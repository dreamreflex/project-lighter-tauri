interface AnsiStyle {
  bold: boolean;
  dim: boolean;
  italic: boolean;
  underline: boolean;
  fgColor: string | null;
  bgColor: string | null;
}

const COLOR_MAP: Record<number, string> = {
  30: "#000000", 31: "#cd3131", 32: "#0dbc79", 33: "#e5e510",
  34: "#2472c8", 35: "#bc3fbc", 36: "#11a8cd", 37: "#e5e5e5",
  90: "#666666", 91: "#f14c4c", 92: "#23d18b", 93: "#f5f543",
  94: "#3b8eea", 95: "#d670d6", 96: "#29b8db", 97: "#e5e5e5",
  40: "#000000", 41: "#cd3131", 42: "#0dbc79", 43: "#e5e510",
  44: "#2472c8", 45: "#bc3fbc", 46: "#11a8cd", 47: "#e5e5e5",
};

const STANDARD_16: string[] = [
  "#000000", "#800000", "#008000", "#808000",
  "#000080", "#800080", "#008080", "#c0c0c0",
  "#808080", "#ff0000", "#00ff00", "#ffff00",
  "#0000ff", "#ff00ff", "#00ffff", "#ffffff",
];

function get256Color(index: number): string | null {
  if (index < 16) return STANDARD_16[index] ?? null;
  if (index < 232) {
    const i = index - 16;
    const toRGB = (n: number) => (n === 0 ? 0 : 55 + n * 40);
    return `rgb(${toRGB(Math.floor(i / 36))}, ${toRGB(Math.floor((i % 36) / 6))}, ${toRGB(i % 6)})`;
  }
  const gray = (index - 232) * 10 + 8;
  return `rgb(${gray}, ${gray}, ${gray})`;
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

const styleStates = new Map<string, AnsiStyle>();

function resetStyle(s: AnsiStyle) {
  s.bold = false;
  s.dim = false;
  s.italic = false;
  s.underline = false;
  s.fgColor = null;
  s.bgColor = null;
}

function processAnsiCode(codes: number[], s: AnsiStyle) {
  let i = 0;
  while (i < codes.length) {
    const c = codes[i];
    if (c === 0) { resetStyle(s); i++; }
    else if (c === 1) { s.bold = true; i++; }
    else if (c === 2) { s.dim = true; i++; }
    else if (c === 3) { s.italic = true; i++; }
    else if (c === 4) { s.underline = true; i++; }
    else if (c === 22) { s.bold = false; s.dim = false; i++; }
    else if (c === 23) { s.italic = false; i++; }
    else if (c === 24) { s.underline = false; i++; }
    else if (c === 39) { s.fgColor = null; i++; }
    else if (c === 49) { s.bgColor = null; i++; }
    else if (c === 38 || c === 48) {
      const isFg = c === 38;
      if (i + 1 < codes.length) {
        const mode = codes[i + 1];
        if (mode === 5 && i + 2 < codes.length) {
          const color = get256Color(codes[i + 2]);
          if (isFg) s.fgColor = color; else s.bgColor = color;
          i += 3;
        } else if (mode === 2 && i + 4 < codes.length) {
          const color = `rgb(${codes[i + 2]}, ${codes[i + 3]}, ${codes[i + 4]})`;
          if (isFg) s.fgColor = color; else s.bgColor = color;
          i += 5;
        } else { i++; }
      } else { i++; }
    } else if ((c >= 30 && c <= 37) || (c >= 90 && c <= 97)) {
      s.fgColor = COLOR_MAP[c] ?? null; i++;
    } else if (c >= 40 && c <= 47) {
      s.bgColor = COLOR_MAP[c] ?? null; i++;
    } else if (c >= 100 && c <= 107) {
      s.bgColor = COLOR_MAP[c - 60] ?? null; i++;
    } else { i++; }
  }
}

function getStyleString(s: AnsiStyle): string | null {
  const css: string[] = [];
  if (s.bold) css.push("font-weight:bold");
  if (s.dim) css.push("opacity:0.5");
  if (s.italic) css.push("font-style:italic");
  if (s.underline) css.push("text-decoration:underline");
  if (s.fgColor) css.push(`color:${s.fgColor}`);
  if (s.bgColor) css.push(`background-color:${s.bgColor}`);
  return css.length > 0 ? css.join(";") : null;
}

export function ansiToHtml(text: string, projectId: string): string {
  if (!styleStates.has(projectId)) {
    styleStates.set(projectId, {
      bold: false, dim: false, italic: false, underline: false,
      fgColor: null, bgColor: null,
    });
  }
  const state = styleStates.get(projectId)!;
  const ansiRegex = /[\u001b\u009b]\[([0-9;]*)([a-zA-Z])/g;

  let html = "";
  let lastIndex = 0;
  let match: RegExpExecArray | null;

  while ((match = ansiRegex.exec(text)) !== null) {
    const before = text.substring(lastIndex, match.index);
    if (before) {
      const style = getStyleString(state);
      const escaped = escapeHtml(before);
      html += style ? `<span style="${style}">${escaped}</span>` : escaped;
    }
    if (match[2] === "m") {
      const codes = match[1].split(";").map((c) => parseInt(c) || 0);
      processAnsiCode(codes, state);
    }
    lastIndex = match.index + match[0].length;
  }

  const remaining = text.substring(lastIndex);
  if (remaining) {
    const style = getStyleString(state);
    const escaped = escapeHtml(remaining);
    html += style ? `<span style="${style}">${escaped}</span>` : escaped;
  }

  return html;
}

export function resetAnsiState(projectId: string) {
  styleStates.delete(projectId);
}
