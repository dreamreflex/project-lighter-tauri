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
