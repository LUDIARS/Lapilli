import { SENTIMENT_LEXICON } from "./lexicon.js";

export type Polarity = "positive" | "negative" | "neutral";

export interface SentimentResult {
  polarity: Polarity;
  score: number;
  confidence: number;
  tier: 0 | 1 | 2;
}

export interface Signal {
  upvotes?: number;
  votedUp?: boolean;
}

export type SentimentLlmResponse =
  | { ok: true; text: string }
  | { ok: false; error?: string };

export interface SentimentLlmClient {
  invoke(request: { system: string; prompt: string; maxTokens: number }): Promise<SentimentLlmResponse>;
}

export interface CascadeClients {
  local?: SentimentLlmClient;
  main?: SentimentLlmClient;
}

const TIER_ZERO_THRESHOLD = 0.3;
const TIER_ONE_THRESHOLD = 0.65;
const SYSTEM_PROMPT =
  'You are a sentiment classifier for game reviews/comments. Reply ONLY with a JSON object: {"polarity":"positive"|"negative"|"neutral","confidence":0.0..1.0}. No other text.';

function tierZeroScore(text: string, signal?: Signal): number {
  const normalized = text.toLowerCase();
  let polarity = 0;
  let hits = 0;
  for (const [word, score] of Object.entries(SENTIMENT_LEXICON.polarity)) {
    if (normalized.includes(word.toLowerCase())) {
      polarity += score;
      hits += 1;
    }
  }
  if (signal?.votedUp === true) {
    polarity += 1.5;
    hits += 1;
  } else if (signal?.votedUp === false) {
    polarity -= 1.5;
    hits += 1;
  }
  if (signal?.upvotes && signal.upvotes > 0) {
    const boost = Math.min(1, Math.log10(signal.upvotes + 1) / 3);
    polarity += boost * (hits > 0 ? Math.sign(polarity) : 0);
  }
  return hits > 0 ? Math.max(-1, Math.min(1, polarity / Math.max(2, hits))) : 0;
}

function resultFromScore(score: number, tier: 0 | 1 | 2): SentimentResult {
  return {
    polarity: score > 0.05 ? "positive" : score < -0.05 ? "negative" : "neutral",
    score,
    confidence: Math.abs(score),
    tier,
  };
}

function buildPrompt(text: string): string {
  return `Classify the sentiment of this game review/comment:\n\n"${text.slice(0, 300)}"`;
}

function parseResponse(raw: string): { polarity: Polarity; confidence: number } | null {
  try {
    const match = raw.match(/\{[\s\S]*\}/);
    if (!match) return null;
    const value = JSON.parse(match[0]) as Record<string, unknown>;
    if (!['positive', 'negative', 'neutral'].includes(String(value.polarity))) return null;
    return {
      polarity: value.polarity as Polarity,
      confidence: Math.max(0, Math.min(1, Number(value.confidence ?? 0))),
    };
  } catch {
    return null;
  }
}

async function invoke(client: SentimentLlmClient, text: string, tier: 1 | 2): Promise<SentimentResult | null> {
  const response = await client.invoke({ system: SYSTEM_PROMPT, prompt: buildPrompt(text), maxTokens: 64 });
  if (!response.ok) return null;
  const parsed = parseResponse(response.text);
  if (!parsed) return null;
  const score = parsed.polarity === "positive"
    ? parsed.confidence
    : parsed.polarity === "negative"
      ? -parsed.confidence
      : 0;
  return { polarity: parsed.polarity, score, confidence: parsed.confidence, tier };
}

export async function cascadeSentiment(
  text: string,
  signal: Signal | undefined,
  clients: CascadeClients,
): Promise<SentimentResult> {
  if (!text.trim()) return { polarity: "neutral", score: 0, confidence: 0, tier: 0 };
  const tierZero = tierZeroScore(text, signal);
  if (Math.abs(tierZero) >= TIER_ZERO_THRESHOLD) return resultFromScore(tierZero, 0);
  if (clients.local) {
    const local = await invoke(clients.local, text, 1);
    if (local && local.confidence >= TIER_ONE_THRESHOLD) return local;
  }
  if (clients.main) {
    return (await invoke(clients.main, text, 2)) ?? {
      polarity: "neutral",
      score: 0,
      confidence: 0,
      tier: 2,
    };
  }
  return resultFromScore(tierZero, 0);
}
