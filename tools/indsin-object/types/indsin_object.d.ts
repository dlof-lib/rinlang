// Type definitions for indsin-object 4.x — https://github.com/ (Rin language, tools/indsin-object)
// MIT License

export as namespace IndsinObject;

/** A node of an Indsin Fabric. Engine frames carry geometry (x/y/w/h) → "absolute" mode; logical trees omit it → "flow" mode. */
export interface FabricNode {
  kind: string;
  name?: string;
  role?: string;
  sourceTag?: string;
  customTag?: string;
  line?: number;
  x?: number; y?: number; w?: number; h?: number;
  resolvedColor?: string | number;
  resolvedAlpha?: number;
  resolvedBorderColor?: string | number;
  attrs?: Record<string, string | number | boolean | null | undefined>;
  a11y?: { role?: string; label?: string; disabled?: boolean; [k: string]: unknown };
  interaction?: Record<string, unknown>;
  children?: FabricNode[];
  [k: string]: unknown;
}

export interface Overlay { name: string; scrim?: boolean; scrimRect?: { x: number; y: number; w: number; h: number }; [k: string]: unknown }

/** The JSON envelope produced by the Indsin engine (`render` / session frames). */
export interface EngineFrame {
  ok: boolean;
  strandsMeasured?: number;
  cacheHits?: number;
  animating?: boolean;
  overlays?: Overlay[];
  paint?: unknown[];
  fabric: FabricNode;
  error?: string;
}

export type IndsinInput = EngineFrame | FabricNode | string;
export type ThemeName = 'Dark' | 'Light' | 'Midnight' | 'Ocean' | 'Sunset' | 'Slate' | 'HighContrastDark' | 'HighContrastLight' | 'Sepia' | 'Forest' | 'Rose';
export type ThemeRole = 'primary' | 'secondary' | 'success' | 'danger' | 'warning' | 'info' | 'neutral' | 'surface' | 'background' | 'text' | 'text_muted' | 'border';
export interface Theme { name: string; [role: string]: string | number | undefined }

export interface Options {
  /** Built-in theme name, 'auto' (follows prefers-color-scheme) or a custom theme object. Default 'dark'. */
  theme?: ThemeName | Lowercase<ThemeName> | 'auto' | Theme;
  /** Text direction. Default 'auto'. */
  dir?: 'ltr' | 'rtl' | 'auto';
  /** Viewport width used for responsive `_sm/_md/_lg/_xl` overrides in flow mode. Default 390. */
  width?: number;
  responsive?: boolean;
  lang?: string;
  title?: string;
  /** Return false/null to refuse a URL, a string to rewrite it, or undefined to use the default policy. */
  urlPolicy?: ((url: string, purpose: 'link' | 'image' | 'media' | 'iframe') => string | boolean | null | undefined) | null;
  /** Permit file:/content:/android-asset: URLs for media (never for links). Default false. */
  allowFileUrls?: boolean;
  objects?: Record<string, unknown> | null;
  animations?: boolean;
  /** Emit the interpreter script with toHtml()/toDocument(). Default true. */
  interactive?: boolean;
  script?: boolean;
  fit?: boolean;
  navigate?: boolean;
  className?: string;
  targetOrigin?: string;
  onEvent?: (event: IndsinEvent) => void;
}

export interface TranslateResult {
  html: string;
  css: string;
  mode: 'absolute' | 'flow';
  width: number | undefined;
  height: number | undefined;
  theme: string;
  issues: Issue[];
  unknown: string[];
}

export interface Issue { code?: string; where?: string; message: string; [k: string]: unknown }
export interface ValidationResult { ok: boolean; errors: Issue[]; warnings: Issue[]; [k: string]: unknown }

export type IndsinEventType = 'tap' | 'doubletap' | 'longpress' | 'hoverenter' | 'hoverexit' | 'change' | 'select' | 'remove' | 'page' | 'close' | 'error' | 'ready' | 'navigate';
/** Normalised event emitted by the interpreter on every platform bridge. */
export interface IndsinEvent {
  type: IndsinEventType | (string & {});
  /** Element name (`@view.Kind=name`). */
  id?: string;
  /** Stable path of the node inside the fabric. */
  path?: string;
  kind?: string;
  x?: number; y?: number;
  value?: unknown;
  index?: number;
  message?: string;
  line?: number;
  [k: string]: unknown;
}

export interface Controller {
  readonly element: HTMLElement;
  render(input: IndsinInput): void;
  applyFrame(input: IndsinInput): void;
  setTheme(theme: Options['theme']): void;
  configure(options: Partial<Options>): void;
  on(listener: (event: IndsinEvent) => void): () => void;
  setOpen(name: string, open: boolean): boolean;
  toHtml(options?: Options): string;
  attachSession(bridge: unknown): void;
  attachWasm(module: unknown, session?: unknown): void;
  detach(): void;
  destroy(): void;
  issues(): Issue[];
}

export interface KindInfo { name: string; key?: string; [k: string]: unknown }
export type HostComponent = (node: FabricNode, attrs: AttrReader, helpers: ComponentHelpers) => string | object;
export interface AttrReader {
  raw: Record<string, unknown>;
  get(key: string): unknown; has(key: string): boolean;
  str(key: string, fallback?: string): string; num(key: string, fallback?: number): number; bool(key: string, fallback?: boolean): boolean;
  first(keys: string[]): unknown;
}
export interface ComponentHelpers {
  esc(s: unknown): string; abs: boolean; id: string;
  kids(opts?: unknown): string; finish(descriptor: object): string;
  color(v: unknown): string | null; safeUrl(url: string, purpose: string): string | null;
  [k: string]: unknown;
}

export const version: string;
export function translate(input: IndsinInput, options?: Options): TranslateResult;
export function toHtml(input: IndsinInput, options?: Options): string;
export function toDocument(input: IndsinInput, options?: Options): string;
export function mount(target: string | HTMLElement, input?: IndsinInput, options?: Options): Controller;
export function validate(input: IndsinInput): ValidationResult;
export function applyResponsive(node: FabricNode, width: number): FabricNode;
export function breakpointOf(width: number): 'xs' | 'sm' | 'md' | 'lg' | 'xl';
export function runtimeSource(): string;
export function stylesheet(): string;
export function registerTheme(theme: Theme): void;
export function themeFromSeed(seed: string, name?: string, dark?: boolean): Theme;
export function themes(): string[];
export function theme(name: string): Theme | null;
export function registerIcon(name: string, glyph: string): void;
export function icons(): string[];
export function registerComponent(tag: string, render: HostComponent): void;
export function registerObject(id: string, data: unknown): void;
export function kinds(): string[];
export function catalog(): Record<string, unknown>;
export function aliases(): Record<string, string>;
export function supports(kind: string): boolean;
export function resolveKind(kind: string): string | null;
export function tokens(): Record<string, unknown>;
export function sample(): FabricNode;
export function selfTest(): { ok: boolean; rendered: number; [k: string]: unknown };
export function glue(language: 'kotlin' | 'swift' | 'electron' | 'react-native' | 'webview2' | string): string;
export function unsupported(): string[];
