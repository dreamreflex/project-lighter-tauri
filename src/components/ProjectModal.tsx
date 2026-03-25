import { useState, useEffect } from "react";
import { open } from "@tauri-apps/plugin-dialog";
import type { Project, CommandItem } from "../types";

interface Props {
  visible: boolean;
  project: Project | null;
  onSave: (project: Project) => void;
  onClose: () => void;
}

interface CommandForm {
  key: number;
  name: string;
  command: string;
}

let keyCounter = 0;

export default function ProjectModal({ visible, project, onSave, onClose }: Props) {
  const [name, setName] = useState("");
  const [workingDir, setWorkingDir] = useState("");
  const [commands, setCommands] = useState<CommandForm[]>([]);

  useEffect(() => {
    if (visible) {
      if (project) {
        setName(project.name || "");
        setWorkingDir(project.workingDir || "");
        const cmds = project.commands
          ? project.commands.map((c) => ({ key: ++keyCounter, name: c.name || "", command: c.command }))
          : project.command
          ? [{ key: ++keyCounter, name: "", command: project.command }]
          : [{ key: ++keyCounter, name: "", command: "" }];
        setCommands(cmds);
      } else {
        setName("");
        setWorkingDir("");
        setCommands([{ key: ++keyCounter, name: "", command: "" }]);
      }
    }
  }, [visible, project]);

  if (!visible) return null;

  const addCommand = () => {
    setCommands([...commands, { key: ++keyCounter, name: "", command: "" }]);
  };

  const removeCommand = (key: number) => {
    setCommands(commands.filter((c) => c.key !== key));
  };

  const updateCommand = (key: number, field: "name" | "command", value: string) => {
    setCommands(commands.map((c) => (c.key === key ? { ...c, [field]: value } : c)));
  };

  const handleSelectDir = async () => {
    const selected = await open({ directory: true });
    if (typeof selected === "string") {
      setWorkingDir(selected);
    }
  };

  const handleSave = () => {
    if (!name.trim()) return;
    const validCommands = commands.filter((c) => c.command.trim());
    if (validCommands.length === 0) return;

    const cmdItems: CommandItem[] = validCommands.map((c) => ({
      name: c.name.trim() || undefined,
      command: c.command.trim(),
    }));

    onSave({
      id: project?.id || String(Date.now()),
      name: name.trim(),
      commands: cmdItems.length > 1 ? cmdItems : undefined,
      command: cmdItems.length === 1 ? cmdItems[0].command : undefined,
      workingDir: workingDir.trim() || undefined,
    });
  };

  return (
    <div className="modal show" onClick={(e) => e.target === e.currentTarget && onClose()}>
      <div className="modal-content modal-large" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>{project ? "编辑项目" : "新建项目"}</h2>
          <button className="close-btn" onClick={onClose}>&times;</button>
        </div>
        <div className="modal-body">
          <div className="form-group">
            <label>项目名称 *</label>
            <input
              type="text"
              className="form-control"
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="例如：前端项目"
            />
          </div>
          <div className="form-group">
            <label>工作目录</label>
            <div className="input-group">
              <input
                type="text"
                className="form-control"
                value={workingDir}
                onChange={(e) => setWorkingDir(e.target.value)}
                placeholder="例如：D:\project"
              />
              <button type="button" className="btn btn-secondary" onClick={handleSelectDir}>
                选择目录
              </button>
            </div>
          </div>
          <div className="form-group">
            <label>命令流 *</label>
            <div>
              {commands.map((cmd, i) => (
                <div className="command-item" key={cmd.key}>
                  <div className="command-item-header">
                    <span className="command-item-title">命令 {i + 1}</span>
                    {commands.length > 1 && (
                      <button
                        type="button"
                        className="btn btn-danger btn-sm"
                        onClick={() => removeCommand(cmd.key)}
                      >
                        删除
                      </button>
                    )}
                  </div>
                  <div className="command-item-body">
                    <div className="form-group">
                      <label>命令名称</label>
                      <input
                        type="text"
                        className="form-control"
                        value={cmd.name}
                        onChange={(e) => updateCommand(cmd.key, "name", e.target.value)}
                        placeholder="例如：启动服务"
                      />
                    </div>
                    <div className="form-group">
                      <label>命令内容 *</label>
                      <input
                        type="text"
                        className="form-control"
                        value={cmd.command}
                        onChange={(e) => updateCommand(cmd.key, "command", e.target.value)}
                        placeholder="例如：npm run dev"
                      />
                    </div>
                  </div>
                </div>
              ))}
            </div>
            <button type="button" className="btn btn-secondary btn-sm" onClick={addCommand}>
              + 添加命令
            </button>
          </div>
        </div>
        <div className="modal-footer">
          <button className="btn btn-primary" onClick={handleSave}>保存项目</button>
          <button className="btn btn-secondary" onClick={onClose}>取消</button>
        </div>
      </div>
    </div>
  );
}
