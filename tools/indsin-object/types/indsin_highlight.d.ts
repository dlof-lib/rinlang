export type TokenType = 'ws' | 'text' | 'ident' | 'comment' | 'string' | 'token' | 'color' | 'number' | 'length' | 'keyword' | 'view' | 'kind' | 'custom' | 'viewname' | 'attr' | 'prop' | 'responsive' | 'end' | 'block' | 'func' | 'op' | 'punct' | 'binding';
export interface Token { type: TokenType; text: string; line: number }
export interface HighlightOptions { lineNumbers?: boolean; theme?: 'dark' | 'light' }
export function tokenize(source: string): Token[];
/** Returns `<pre class="ih"><code>…</code></pre>`; all text is HTML-escaped. */
export function highlight(source: string, options?: HighlightOptions): string;
export function stylesheet(): string;
export const palette: { dark: Record<string, string>; light: Record<string, string> };
export const lexicon: { kinds: string[]; attributes: string[]; tokenValues: string[]; keywords: string[] };
export const version: string;
