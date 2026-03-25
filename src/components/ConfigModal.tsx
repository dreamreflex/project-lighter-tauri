import { useState, useEffect } from "react";
import type { Config } from "../types";

interface Props {
  visible: boolean;
  config: Config;
  onSave: (config: Config) => void;
  onClose: () => void;
}

export default function ConfigModal({ visible, config, onSave, onClose }: Props) {
  const [text, setText] = useState("");
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (visible) {
      setText(JSON.stringify(config, null, 2));
      setError(null);
    }
  }, [visible, config]);

  if (!visible) return null;

  const handleSave = () => {
    try {
      const parsed = JSON.parse(text);
      if (!parsed.projects || !Array.isArray(parsed.projects)) {
        setError("配置格式错误: projects 必须是数组");
        return;
      }
      for (let i = 0; i < parsed.projects.length; i++) {
        const p = parsed.projects[i];
        if (!p.id) { setError(`项目 ${i + 1} 缺少 id 字段`); return; }
        if (!p.name) { setError(`项目 ${i + 1} 缺少 name 字段`); return; }
        if (!p.commands && !p.command) { setError(`项目 ${i + 1} 缺少 command 或 commands 字段`); return; }
      }
      setError(null);
      onSave(parsed);
    } catch (e) {
      setError("JSON 解析失败: " + String(e));
    }
  };

  return (
    <div className="modal show" onClick={(e) => e.target === e.currentTarget && onClose()}>
      <div className="modal-content" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>编辑 JSON 配置</h2>
          <button className="close-btn" onClick={onClose}>&times;</button>
        </div>
        <div className="modal-body">
          <textarea
            className="config-textarea"
            value={text}
            onChange={(e) => setText(e.target.value)}
            spellCheck={false}
          />
          {error && <div className="config-error">{error}</div>}
        </div>
        <div className="modal-footer">
          <button className="btn btn-primary" onClick={handleSave}>保存</button>
          <button className="btn btn-secondary" onClick={onClose}>取消</button>
        </div>
      </div>
    </div>
  );
}
