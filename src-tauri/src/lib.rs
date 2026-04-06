use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter, Manager, State};
use tokio::io::AsyncReadExt;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct CommandItem {
    pub name: Option<String>,
    pub command: String,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Project {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub commands: Option<Vec<CommandItem>>,
    #[serde(default)]
    pub command: Option<String>,
    #[serde(rename = "workingDir", default)]
    pub working_dir: Option<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct Config {
    pub projects: Vec<Project>,
}

#[derive(Debug, Serialize, Clone)]
struct OutputPayload {
    #[serde(rename = "projectId")]
    project_id: String,
    #[serde(rename = "type")]
    output_type: String,
    data: String,
}

#[derive(Debug, Serialize, Clone)]
struct ExitPayload {
    #[serde(rename = "projectId")]
    project_id: String,
    code: i32,
}

#[derive(Debug, Serialize, Clone)]
struct ErrorPayload {
    #[serde(rename = "projectId")]
    project_id: String,
    error: String,
}

struct ProcessInfo {
    pid: u32,
}

#[derive(Debug, Serialize, Clone)]
struct PortProcessInfo {
    found: bool,
    pid: Option<u32>,
    #[serde(rename = "processName")]
    process_name: Option<String>,
    message: String,
}

fn get_process_name_by_pid(pid: u32) -> String {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        if let Ok(output) = std::process::Command::new("tasklist")
            .args(["/FI", &format!("PID eq {}", pid), "/FO", "CSV", "/NH"])
            .creation_flags(0x08000000)
            .output()
        {
            let stdout = String::from_utf8_lossy(&output.stdout);
            for line in stdout.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with('"') {
                    if let Some(end) = trimmed[1..].find('"') {
                        return trimmed[1..end + 1].to_string();
                    }
                }
            }
        }
        "unknown".to_string()
    }

    #[cfg(not(windows))]
    {
        if let Ok(output) = std::process::Command::new("ps")
            .args(["-p", &pid.to_string(), "-o", "comm="])
            .output()
        {
            let name = String::from_utf8_lossy(&output.stdout).trim().to_string();
            if !name.is_empty() {
                return name;
            }
        }
        "unknown".to_string()
    }
}

fn find_process_on_port(port: u16) -> Result<Option<(u32, String)>, String> {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let output = std::process::Command::new("netstat")
            .args(["-ano", "-p", "tcp"])
            .creation_flags(0x08000000)
            .output()
            .map_err(|e| format!("执行 netstat 失败: {}", e))?;

        let stdout = String::from_utf8_lossy(&output.stdout);

        for line in stdout.lines() {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 5 && parts[3] == "LISTENING" {
                if let Some(port_str) = parts[1].rsplit(':').next() {
                    if port_str.parse::<u16>().ok() == Some(port) {
                        if let Ok(pid) = parts[4].parse::<u32>() {
                            let name = get_process_name_by_pid(pid);
                            return Ok(Some((pid, name)));
                        }
                    }
                }
            }
        }
        Ok(None)
    }

    #[cfg(not(windows))]
    {
        if let Ok(output) = std::process::Command::new("lsof")
            .args(["-i", &format!(":{}", port), "-P", "-n", "-sTCP:LISTEN"])
            .output()
        {
            let stdout = String::from_utf8_lossy(&output.stdout);
            for line in stdout.lines().skip(1) {
                let parts: Vec<&str> = line.split_whitespace().collect();
                if parts.len() >= 2 {
                    if let Ok(pid) = parts[1].parse::<u32>() {
                        return Ok(Some((pid, parts[0].to_string())));
                    }
                }
            }
            if output.status.success() || output.status.code() == Some(1) {
                return Ok(None);
            }
        }

        if let Ok(output) = std::process::Command::new("ss")
            .args(["-tlnp"])
            .output()
        {
            let stdout = String::from_utf8_lossy(&output.stdout);
            for line in stdout.lines() {
                let parts: Vec<&str> = line.split_whitespace().collect();
                if parts.len() < 4 {
                    continue;
                }
                let local_addr = parts[3];
                if let Some(port_str) = local_addr.rsplit(':').next() {
                    if port_str.parse::<u16>().ok() == Some(port) {
                        if let Some(pid_start) = line.find("pid=") {
                            let rest = &line[pid_start + 4..];
                            let pid_str: String =
                                rest.chars().take_while(|c| c.is_ascii_digit()).collect();
                            if let Ok(pid) = pid_str.parse::<u32>() {
                                let name = if let Some(name_start) = line.find("((\"") {
                                    let name_rest = &line[name_start + 3..];
                                    name_rest
                                        .split('"')
                                        .next()
                                        .unwrap_or("unknown")
                                        .to_string()
                                } else {
                                    get_process_name_by_pid(pid)
                                };
                                return Ok(Some((pid, name)));
                            }
                        }
                    }
                }
            }
            return Ok(None);
        }

        Err("无法查找端口占用信息：lsof 和 ss 命令都不可用".to_string())
    }
}

fn kill_process_by_pid(pid: u32) -> Result<std::process::Output, String> {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        std::process::Command::new("taskkill")
            .args(["/PID", &pid.to_string(), "/F", "/T"])
            .creation_flags(0x08000000)
            .output()
            .map_err(|e| format!("执行 taskkill 失败: {}", e))
    }

    #[cfg(not(windows))]
    {
        std::process::Command::new("kill")
            .args(["-9", &pid.to_string()])
            .output()
            .map_err(|e| format!("执行 kill 失败: {}", e))
    }
}

pub struct AppState {
    processes: Arc<Mutex<HashMap<String, ProcessInfo>>>,
}

fn get_config_path(app: &AppHandle) -> Result<std::path::PathBuf, String> {
    let config_dir = app.path().app_config_dir().map_err(|e| e.to_string())?;
    std::fs::create_dir_all(&config_dir).map_err(|e| e.to_string())?;
    Ok(config_dir.join("config.json"))
}

#[tauri::command]
async fn get_config(app: AppHandle) -> Result<Config, String> {
    let config_path = get_config_path(&app)?;
    if config_path.exists() {
        let data = std::fs::read_to_string(&config_path).map_err(|e| e.to_string())?;
        serde_json::from_str(&data).map_err(|e| e.to_string())
    } else {
        Ok(Config::default())
    }
}

#[tauri::command]
async fn save_config(app: AppHandle, config: Config) -> Result<bool, String> {
    let config_path = get_config_path(&app)?;
    let data = serde_json::to_string_pretty(&config).map_err(|e| e.to_string())?;
    std::fs::write(&config_path, data).map_err(|e| e.to_string())?;
    Ok(true)
}

#[tauri::command]
async fn start_project(
    app: AppHandle,
    state: State<'_, AppState>,
    project_id: String,
    commands: Vec<CommandItem>,
    working_dir: String,
) -> Result<bool, String> {
    // Build combined PowerShell command chain
    let mut combined = String::new();
    for (i, cmd) in commands.iter().enumerate() {
        if i == 0 {
            combined.push_str(&cmd.command);
        } else {
            combined.push_str(&format!("; if ($?) {{ {} }}", cmd.command));
        }
    }

    let encoding_setup = concat!(
        "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; ",
        "[Console]::InputEncoding = [System.Text.Encoding]::UTF8; ",
        "$OutputEncoding = [System.Text.Encoding]::UTF8; ",
        "$env:PYTHONIOENCODING = 'utf-8'; ",
        "chcp 65001 | Out-Null; ",
    );

    let full_command = format!("{}{}", encoding_setup, combined);

    let cwd = if working_dir.is_empty() {
        std::env::current_dir().map_err(|e| e.to_string())?
    } else {
        std::path::PathBuf::from(&working_dir)
    };

    if !cwd.exists() {
        return Err(format!("工作目录不存在: {}", cwd.display()));
    }

    let mut cmd = tokio::process::Command::new("powershell.exe");
    cmd.args([
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        &full_command,
    ])
    .current_dir(&cwd)
    .stdout(std::process::Stdio::piped())
    .stderr(std::process::Stdio::piped())
    .stdin(std::process::Stdio::null())
    .env("PYTHONIOENCODING", "utf-8")
    .env("PYTHONUTF8", "1");

    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
    }

    let mut child = cmd.spawn().map_err(|e| format!("启动进程失败: {}", e))?;
    let pid = child.id().ok_or("无法获取进程ID")?;

    {
        let mut processes = state.processes.lock().map_err(|e| e.to_string())?;
        processes.insert(project_id.clone(), ProcessInfo { pid });
    }

    let stdout = child.stdout.take().ok_or("无法捕获标准输出")?;
    let stderr = child.stderr.take().ok_or("无法捕获标准错误")?;

    // Spawn stdout reader
    let app_out = app.clone();
    let id_out = project_id.clone();
    tokio::spawn(async move {
        let mut reader = tokio::io::BufReader::new(stdout);
        let mut buffer = vec![0u8; 4096];
        loop {
            match reader.read(&mut buffer).await {
                Ok(0) => break,
                Ok(n) => {
                    let text = String::from_utf8_lossy(&buffer[..n]).to_string();
                    let _ = app_out.emit(
                        "project-output",
                        OutputPayload {
                            project_id: id_out.clone(),
                            output_type: "stdout".into(),
                            data: text,
                        },
                    );
                }
                Err(_) => break,
            }
        }
    });

    // Spawn stderr reader
    let app_err = app.clone();
    let id_err = project_id.clone();
    tokio::spawn(async move {
        let mut reader = tokio::io::BufReader::new(stderr);
        let mut buffer = vec![0u8; 4096];
        loop {
            match reader.read(&mut buffer).await {
                Ok(0) => break,
                Ok(n) => {
                    let text = String::from_utf8_lossy(&buffer[..n]).to_string();
                    let _ = app_err.emit(
                        "project-output",
                        OutputPayload {
                            project_id: id_err.clone(),
                            output_type: "stderr".into(),
                            data: text,
                        },
                    );
                }
                Err(_) => break,
            }
        }
    });

    // Spawn exit handler
    let app_exit = app.clone();
    let id_exit = project_id.clone();
    let processes_ref = state.processes.clone();
    tokio::spawn(async move {
        let status = child.wait().await;
        let code = match status {
            Ok(s) => s.code().unwrap_or(-1),
            Err(_) => -1,
        };

        if let Ok(mut procs) = processes_ref.lock() {
            procs.remove(&id_exit);
        }

        let _ = app_exit.emit(
            "project-exit",
            ExitPayload {
                project_id: id_exit,
                code,
            },
        );
    });

    Ok(true)
}

#[tauri::command]
async fn stop_project(
    state: State<'_, AppState>,
    project_id: String,
) -> Result<bool, String> {
    let pid = {
        let mut processes = state.processes.lock().map_err(|e| e.to_string())?;
        processes.remove(&project_id).map(|p| p.pid)
    };

    if let Some(pid) = pid {
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            let _ = std::process::Command::new("taskkill")
                .args(["/pid", &pid.to_string(), "/f", "/t"])
                .creation_flags(0x08000000)
                .output();
        }
        #[cfg(not(windows))]
        {
            let _ = std::process::Command::new("kill")
                .args(["-9", &pid.to_string()])
                .output();
        }
        Ok(true)
    } else {
        Ok(false)
    }
}

#[tauri::command]
async fn export_config_to_file(app: AppHandle, path: String) -> Result<bool, String> {
    let config = get_config(app).await?;
    let data = serde_json::to_string_pretty(&config).map_err(|e| e.to_string())?;
    std::fs::write(&path, data).map_err(|e| e.to_string())?;
    Ok(true)
}

#[tauri::command]
async fn import_config_from_file(path: String) -> Result<Config, String> {
    let data = std::fs::read_to_string(&path).map_err(|e| e.to_string())?;
    serde_json::from_str(&data).map_err(|e| e.to_string())
}

#[tauri::command]
async fn query_port(port: u16) -> Result<PortProcessInfo, String> {
    if port == 0 {
        return Err("端口号不能为 0".to_string());
    }

    match find_process_on_port(port)? {
        Some((pid, name)) => Ok(PortProcessInfo {
            found: true,
            pid: Some(pid),
            process_name: Some(name.clone()),
            message: format!("端口 {} 被 {} (PID: {}) 占用", port, name, pid),
        }),
        None => Ok(PortProcessInfo {
            found: false,
            pid: None,
            process_name: None,
            message: format!("端口 {} 当前没有被占用", port),
        }),
    }
}

#[tauri::command]
async fn kill_port(port: u16) -> Result<PortProcessInfo, String> {
    if port == 0 {
        return Err("端口号不能为 0".to_string());
    }

    match find_process_on_port(port)? {
        Some((pid, name)) => {
            let kill_result = kill_process_by_pid(pid)?;

            if kill_result.status.success() {
                Ok(PortProcessInfo {
                    found: true,
                    pid: Some(pid),
                    process_name: Some(name.clone()),
                    message: format!(
                        "已成功终止端口 {} 上的进程 {} (PID: {})",
                        port, name, pid
                    ),
                })
            } else {
                let stderr = String::from_utf8_lossy(&kill_result.stderr);
                let error_detail = stderr.trim();
                let hint = if cfg!(windows) {
                    "可能需要以管理员权限运行本程序"
                } else {
                    "可能需要使用 sudo 权限运行本程序"
                };
                Err(format!(
                    "无法终止进程 {} (PID: {}): {}。{}",
                    name,
                    pid,
                    if error_detail.is_empty() {
                        "未知错误"
                    } else {
                        error_detail
                    },
                    hint
                ))
            }
        }
        None => Ok(PortProcessInfo {
            found: false,
            pid: None,
            process_name: None,
            message: format!("端口 {} 当前没有被占用，无需终止", port),
        }),
    }
}

pub fn run() {
    let state = AppState {
        processes: Arc::new(Mutex::new(HashMap::new())),
    };

    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .manage(state)
        .invoke_handler(tauri::generate_handler![
            get_config,
            save_config,
            start_project,
            stop_project,
            export_config_to_file,
            import_config_from_file,
            query_port,
            kill_port,
        ])
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { .. } = event {
                let app_state = window.state::<AppState>();
                if let Ok(processes) = app_state.processes.lock() {
                    for (_, info) in processes.iter() {
                        #[cfg(windows)]
                        {
                            use std::os::windows::process::CommandExt;
                            let _ = std::process::Command::new("taskkill")
                                .args(["/pid", &info.pid.to_string(), "/f", "/t"])
                                .creation_flags(0x08000000)
                                .output();
                        }
                        #[cfg(not(windows))]
                        {
                            let _ = std::process::Command::new("kill")
                                .args(["-9", &info.pid.to_string()])
                                .output();
                        }
                    }
                };
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
