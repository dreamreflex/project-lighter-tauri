import { useEffect, useRef } from "react";
import type { Project, CommandItem } from "../types";

interface Props {
  project: Project;
  isRunning: boolean;
  output: string;
  onStart: (id: string) => void;
  onStop: (id: string) => void;
  onEdit: (id: string) => void;
  onDelete: (id: string) => void;
}

function getCommands(project: Project): CommandItem[] {
  if (project.commands && Array.isArray(project.commands)) return project.commands;
  if (project.command) return [{ name: "主命令", command: project.command }];
  return [];
}

export default function ProjectCard({
  project, isRunning, output, onStart, onStop, onEdit, onDelete,
}: Props) {
  const outputRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [output]);

  const commands = getCommands(project);

  return (
    <div className="project-card">
      <div className="project-header">
        <div className="project-name">{project.name || "未命名项目"}</div>
        <div className="project-status">
          <span className={`status-indicator ${isRunning ? "running" : "stopped"}`} />
          <span>{isRunning ? "运行中" : "已停止"}</span>
        </div>
      </div>
      <div className="project-body">
        <div className="project-info">
          {commands.length > 1
            ? commands.map((cmd, i) => (
                <div className="project-info-item" key={i}>
                  <span className="project-info-label">{cmd.name || `命令 ${i + 1}`}:</span>
                  <span>{cmd.command}</span>
                </div>
              ))
            : commands.length === 1 && (
                <div className="project-info-item">
                  <span className="project-info-label">命令:</span>
                  <span>{commands[0].command}</span>
                </div>
              )}
          <div className="project-info-item">
            <span className="project-info-label">工作目录:</span>
            <span>{project.workingDir || "当前目录"}</span>
          </div>
        </div>
        <div className="project-actions">
          <button className="btn btn-primary" disabled={isRunning} onClick={() => onStart(project.id)}>
            启动
          </button>
          <button className="btn btn-danger" disabled={!isRunning} onClick={() => onStop(project.id)}>
            停止
          </button>
          <button className="btn btn-secondary" onClick={() => onEdit(project.id)}>
            编辑
          </button>
          <button className="btn btn-secondary" onClick={() => onDelete(project.id)}>
            删除
          </button>
        </div>
        <div
          ref={outputRef}
          className="project-output"
          dangerouslySetInnerHTML={{ __html: output }}
        />
      </div>
    </div>
  );
}
