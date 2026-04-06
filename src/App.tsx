import { useState, useEffect, useCallback, useRef } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { save, open as openDialog } from "@tauri-apps/plugin-dialog";
import { openUrl } from "@tauri-apps/plugin-opener";
import type { Config, Project, OutputEvent, ExitEvent, CommandItem } from "./types";
import { ansiToHtml, resetAnsiState } from "./utils/ansi";
import ProjectCard from "./components/ProjectCard";
import ProjectModal from "./components/ProjectModal";
import ConfigModal from "./components/ConfigModal";
import PortKiller from "./components/PortKiller";

declare const __GIT_COMMIT__: string;

interface Toast {
  id: number;
  message: string;
  type: "success" | "error";
}

let toastId = 0;

function App() {
  const [config, setConfig] = useState<Config>({ projects: [] });
  const [statuses, setStatuses] = useState<Record<string, string>>({});
  const [outputs, setOutputs] = useState<Record<string, string>>({});
  const [showProjectModal, setShowProjectModal] = useState(false);
  const [showConfigModal, setShowConfigModal] = useState(false);
  const [editingProject, setEditingProject] = useState<Project | null>(null);
  const [toasts, setToasts] = useState<Toast[]>([]);
  const outputsRef = useRef(outputs);
  outputsRef.current = outputs;

  const showToast = useCallback((message: string, type: "success" | "error") => {
    const id = ++toastId;
    setToasts((prev) => [...prev, { id, message, type }]);
    setTimeout(() => {
      setToasts((prev) => prev.filter((t) => t.id !== id));
    }, 3000);
  }, []);

  const loadConfig = useCallback(async () => {
    try {
      const cfg = await invoke<Config>("get_config");
      setConfig(cfg);
    } catch (e) {
      showToast("加载配置失败: " + String(e), "error");
    }
  }, [showToast]);

  useEffect(() => {
    loadConfig();
  }, [loadConfig]);

  useEffect(() => {
    const unlisteners: Array<() => void> = [];

    listen<OutputEvent>("project-output", (event) => {
      const { projectId, data } = event.payload;
      const html = ansiToHtml(data, projectId);
      setOutputs((prev) => ({
        ...prev,
        [projectId]: (prev[projectId] || "") + html,
      }));
    }).then((unlisten) => unlisteners.push(unlisten));

    listen<ExitEvent>("project-exit", (event) => {
      const { projectId, code } = event.payload;
      setStatuses((prev) => ({ ...prev, [projectId]: "stopped" }));
      setOutputs((prev) => ({
        ...prev,
        [projectId]:
          (prev[projectId] || "") +
          `\n<span style="color:#8a8a8a">[进程已退出，退出码: ${code}]</span>\n`,
      }));
    }).then((unlisten) => unlisteners.push(unlisten));

    return () => {
      unlisteners.forEach((fn) => fn());
    };
  }, []);

  function getProjectCommands(project: Project): CommandItem[] {
    if (project.commands && Array.isArray(project.commands)) return project.commands;
    if (project.command) return [{ name: "主命令", command: project.command }];
    return [];
  }

  const handleStart = useCallback(
    async (projectId: string) => {
      const project = config.projects.find((p) => p.id === projectId);
      if (!project) return;

      const commands = getProjectCommands(project);
      if (commands.length === 0) {
        showToast("项目没有配置任何命令", "error");
        return;
      }

      resetAnsiState(projectId);

      const commandList = commands
        .map((cmd, i) => `${i + 1}. [${cmd.name || `步骤 ${i + 1}`}] ${cmd.command}`)
        .join("<br>");
      const startMsg = `准备在 PowerShell 中执行以下命令序列:<br>${commandList}<br><br>${"=".repeat(50)}<br><br>`;
      setOutputs((prev) => ({ ...prev, [projectId]: startMsg }));

      try {
        const result = await invoke<boolean>("start_project", {
          projectId,
          commands,
          workingDir: project.workingDir || "",
        });
        if (result) {
          setStatuses((prev) => ({ ...prev, [projectId]: "running" }));
        }
      } catch (e) {
        showToast("启动项目失败: " + String(e), "error");
      }
    },
    [config, showToast]
  );

  const handleStop = useCallback(
    async (projectId: string) => {
      try {
        await invoke<boolean>("stop_project", { projectId });
        setStatuses((prev) => ({ ...prev, [projectId]: "stopped" }));
      } catch (e) {
        showToast("停止项目失败: " + String(e), "error");
      }
    },
    [showToast]
  );

  const handleEdit = useCallback(
    (projectId: string) => {
      const project = config.projects.find((p) => p.id === projectId);
      if (project) {
        setEditingProject(project);
        setShowProjectModal(true);
      }
    },
    [config]
  );

  const handleDelete = useCallback(
    async (projectId: string) => {
      if (statuses[projectId] === "running") {
        await handleStop(projectId);
      }

      const newConfig = {
        ...config,
        projects: config.projects.filter((p) => p.id !== projectId),
      };
      try {
        await invoke("save_config", { config: newConfig });
        setConfig(newConfig);
        showToast("项目已删除", "success");
      } catch (e) {
        showToast("删除失败: " + String(e), "error");
      }
    },
    [config, statuses, handleStop, showToast]
  );

  const handleSaveProject = useCallback(
    async (project: Project) => {
      const existing = config.projects.findIndex((p) => p.id === project.id);
      let newProjects: Project[];
      if (existing !== -1) {
        newProjects = [...config.projects];
        newProjects[existing] = project;
      } else {
        newProjects = [...config.projects, project];
      }

      const newConfig = { projects: newProjects };
      try {
        await invoke("save_config", { config: newConfig });
        setConfig(newConfig);
        setShowProjectModal(false);
        setEditingProject(null);
        showToast("项目保存成功", "success");
      } catch (e) {
        showToast("保存失败: " + String(e), "error");
      }
    },
    [config, showToast]
  );

  const handleSaveConfig = useCallback(
    async (newConfig: Config) => {
      try {
        await invoke("save_config", { config: newConfig });
        for (const [id, status] of Object.entries(statuses)) {
          if (status === "running") await handleStop(id);
        }
        setConfig(newConfig);
        setStatuses({});
        setShowConfigModal(false);
        showToast("配置保存成功", "success");
      } catch (e) {
        showToast("保存配置失败: " + String(e), "error");
      }
    },
    [statuses, handleStop, showToast]
  );

  const handleExport = useCallback(async () => {
    try {
      const path = await save({
        title: "导出配置",
        defaultPath: "project-config.json",
        filters: [{ name: "JSON文件", extensions: ["json"] }],
      });
      if (path) {
        await invoke("export_config_to_file", { path });
        showToast("配置已导出", "success");
      }
    } catch (e) {
      showToast("导出失败: " + String(e), "error");
    }
  }, [showToast]);

  const handleImport = useCallback(async () => {
    try {
      const selected = await openDialog({
        title: "导入配置",
        filters: [{ name: "JSON文件", extensions: ["json"] }],
      });
      if (typeof selected === "string") {
        if (!window.confirm("导入配置将覆盖当前配置，是否继续？")) return;
        const imported = await invoke<Config>("import_config_from_file", { path: selected });
        await handleSaveConfig(imported);
        showToast("配置导入成功", "success");
      }
    } catch (e) {
      showToast("导入失败: " + String(e), "error");
    }
  }, [handleSaveConfig, showToast]);

  return (
    <div className="container">
      <header>
        <h1>
          项目启动器
        </h1>
        <div className="header-actions">
          <button className="btn btn-primary" onClick={() => { setEditingProject(null); setShowProjectModal(true); }}>
            新建项目
          </button>
          <button className="btn btn-secondary" onClick={handleExport}>导出配置</button>
          <button className="btn btn-secondary" onClick={handleImport}>导入配置</button>
          <button className="btn btn-secondary" onClick={() => setShowConfigModal(true)}>编辑JSON</button>
          <button className="btn btn-secondary" onClick={loadConfig}>刷新</button>
        </div>
      </header>

      <PortKiller onToast={showToast} />

      {config.projects.length === 0 ? (
        <div className="empty-state">
          <p>暂无项目配置</p>
          <button className="btn btn-primary" onClick={() => { setEditingProject(null); setShowProjectModal(true); }}>
            添加第一个项目
          </button>
        </div>
      ) : (
        <div className="projects-container">
          {config.projects.map((project) => (
            <ProjectCard
              key={project.id}
              project={project}
              isRunning={statuses[project.id] === "running"}
              output={outputs[project.id] || ""}
              onStart={handleStart}
              onStop={handleStop}
              onEdit={handleEdit}
              onDelete={handleDelete}
            />
          ))}
        </div>
      )}

      <footer>
        <div className="footer-content">
          <div className="footer-info">
            <span className="app-name">项目启动器</span>
            <span className="version">v1.0.0 · {__GIT_COMMIT__}</span>
          </div>
          <div className="footer-desc">多项目一键启动管理工具，让开发更高效。</div>
          <a
            className="footer-link"
            href="#"
            onClick={(e) => { e.preventDefault(); openUrl("https://github.com/dreamreflex/project-lighter-tauri"); }}
          >
            github.com/dreamreflex/project-lighter-tauri
          </a>
        </div>
      </footer>

      <ProjectModal
        visible={showProjectModal}
        project={editingProject}
        onSave={handleSaveProject}
        onClose={() => { setShowProjectModal(false); setEditingProject(null); }}
      />

      <ConfigModal
        visible={showConfigModal}
        config={config}
        onSave={handleSaveConfig}
        onClose={() => setShowConfigModal(false)}
      />

      <div className="toast-container">
        {toasts.map((toast) => (
          <div key={toast.id} className={`toast toast-${toast.type}`}>
            {toast.message}
          </div>
        ))}
      </div>
    </div>
  );
}

export default App;
