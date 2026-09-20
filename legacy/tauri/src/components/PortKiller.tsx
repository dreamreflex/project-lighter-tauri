import { useState, useCallback } from "react";
import { invoke } from "@tauri-apps/api/core";
import type { PortProcessInfo } from "../types";

interface PortKillerProps {
  onToast: (message: string, type: "success" | "error") => void;
}

export default function PortKiller({ onToast }: PortKillerProps) {
  const [port, setPort] = useState("");
  const [result, setResult] = useState<PortProcessInfo | null>(null);
  const [loading, setLoading] = useState(false);
  const [expanded, setExpanded] = useState(false);

  const handleQuery = useCallback(async () => {
    const portNum = parseInt(port, 10);
    if (!portNum || portNum < 1 || portNum > 65535) {
      onToast("请输入有效的端口号 (1-65535)", "error");
      return;
    }

    setLoading(true);
    setResult(null);
    try {
      const info = await invoke<PortProcessInfo>("query_port", { port: portNum });
      setResult(info);
    } catch (e) {
      onToast("查询端口失败: " + String(e), "error");
    } finally {
      setLoading(false);
    }
  }, [port, onToast]);

  const handleKill = useCallback(async () => {
    const portNum = parseInt(port, 10);
    if (!portNum || portNum < 1 || portNum > 65535) {
      onToast("请输入有效的端口号 (1-65535)", "error");
      return;
    }

    setLoading(true);
    setResult(null);
    try {
      const info = await invoke<PortProcessInfo>("kill_port", { port: portNum });
      setResult(info);
      if (info.found) {
        onToast(info.message, "success");
      } else {
        onToast(info.message, "success");
      }
    } catch (e) {
      onToast(String(e), "error");
    } finally {
      setLoading(false);
    }
  }, [port, onToast]);

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === "Enter") {
        handleQuery();
      }
    },
    [handleQuery]
  );

  return (
    <div className="tool-section">
      <button
        className="tool-section-toggle"
        onClick={() => setExpanded(!expanded)}
      >
        <span className="tool-section-title">端口管理工具</span>
        <span className={`tool-section-arrow ${expanded ? "expanded" : ""}`}>
          ›
        </span>
      </button>

      {expanded && (
        <div className="tool-section-body">
          <div className="port-killer-row">
            <input
              type="number"
              className="form-control port-input"
              placeholder="端口号，如 8080"
              value={port}
              onChange={(e) => setPort(e.target.value)}
              onKeyDown={handleKeyDown}
              min={1}
              max={65535}
            />
            <button
              className="btn btn-secondary"
              onClick={handleQuery}
              disabled={loading || !port}
            >
              {loading ? "查询中..." : "查询端口"}
            </button>
            <button
              className="btn btn-danger"
              onClick={handleKill}
              disabled={loading || !port}
            >
              {loading ? "处理中..." : "结束占用端口进程"}
            </button>
          </div>

          {result && (
            <div
              className={`port-result ${result.found ? "port-result-found" : "port-result-empty"}`}
            >
              <span className="port-result-text">{result.message}</span>
              {result.found && result.pid && (
                <span className="port-result-detail">
                  PID: {result.pid}
                  {result.processName && ` · ${result.processName}`}
                </span>
              )}
            </div>
          )}
        </div>
      )}
    </div>
  );
}
