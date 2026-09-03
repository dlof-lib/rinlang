// RinHTML — type declarations for the public JS API (web/rinhtml/rinhtml.js)

export type RinAllow = string; // e.g. "dom storage" — space-separated capability list

export interface RinRunOptions {
  allow?: RinAllow;
  root?: Document | HTMLElement;
  src?: string; // original .rin path, required for app.reload()
}

export declare class RinApp {
  readonly sessionId: number;
  readonly globals: Record<string, string | number | boolean | null>;
  call(fnName: string, ...args: Array<string | number | boolean>): { ok: boolean; error?: string; globals: Record<string, unknown> };
  get(name: string): string | number | boolean | null | undefined;
  set(name: string, value: string | number | boolean): void;
  mount(root?: Document | HTMLElement): void;
  unmount(): void;
  reload(): Promise<RinApp>;
  on(event: 'update' | 'error' | string, handler: (detail: unknown) => void): void;
  emit(event: string, detail?: unknown): void;
  destroy(): void;
}

export interface RinHTML {
  apps: Record<string, RinApp>;
  load(url: string): Promise<string>;
  run(source: string, opts?: RinRunOptions): Promise<RinApp>;
  runFile(src: string, opts?: RinRunOptions): Promise<RinApp>;
  mount(app: RinApp, root?: Document | HTMLElement): void;
  unmount(app: RinApp): void;
  reload(app: RinApp): Promise<RinApp>;
  call(app: RinApp, fnName: string, ...args: unknown[]): unknown;
  get(app: RinApp, name: string): unknown;
  set(app: RinApp, name: string, value: unknown): void;
  on(app: RinApp, event: string, handler: (detail: unknown) => void): void;
  emit(app: RinApp, event: string, detail?: unknown): void;
  bind(app: RinApp, root?: Document | HTMLElement): void;
}

declare global {
  interface Window { rinhtml: RinHTML; }

  interface HTMLElementTagNameMap {
    'rin-app': HTMLElement & { rinApp?: RinApp };
    'rin-container': HTMLElement & { rinApp?: RinApp };
    'rin-run': HTMLElement & { rinApp?: RinApp };
  }
}
