export interface CommandItem {
  name?: string;
  command: string;
}

export interface Project {
  id: string;
  name: string;
  commands?: CommandItem[];
  command?: string;
  workingDir?: string;
}

export interface Config {
  projects: Project[];
}

export interface OutputEvent {
  projectId: string;
  type: "stdout" | "stderr";
  data: string;
}

export interface ExitEvent {
  projectId: string;
  code: number;
}
