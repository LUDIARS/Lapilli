import { SENTIMENT_LEXICON } from "./lexicon.js";

export const EMO_KEYS = Object.freeze(Object.keys(SENTIMENT_LEXICON.emotions));
export const ASP_KEYS = Object.freeze(Object.keys(SENTIMENT_LEXICON.aspects));

export const VECTOR_SPEC = Object.freeze([
  "emo.valence",
  "emo.arousal",
  ...EMO_KEYS.map((emotion) => `emo.${emotion}`),
  ...ASP_KEYS.map((aspect) => `asp.${aspect}`),
  "meta.positive_ratio",
  "meta.volume_log",
]);

/** Backward-compatible alias used by Discutere. */
export const VECTOR_DIMS = VECTOR_SPEC;
export const DIM = VECTOR_SPEC.length;
export const VECTOR_SPEC_VERSION = 1;

export interface SentimentSignal {
  votedUp?: boolean;
  upvotes?: number;
}

export interface TextFeatures {
  valence: number;
  emotions: Record<string, number>;
  aspectMentions: Record<string, number>;
  arousal: number;
}

const clamp01 = (value: number): number => Math.max(0, Math.min(1, value));
const round4 = (value: number): number => Number(value.toFixed(4));

function countHits(text: string, words: string[]): number {
  return words.reduce((sum, word) => sum + (text.includes(word.toLowerCase()) ? 1 : 0), 0);
}

export function extractTextFeatures(text: string, signal: SentimentSignal = {}): TextFeatures {
  const normalized = (text || "").toLowerCase();
  let polarity = 0;
  let polarityHits = 0;
  for (const [word, score] of Object.entries(SENTIMENT_LEXICON.polarity)) {
    if (normalized.includes(word.toLowerCase())) {
      polarity += score;
      polarityHits += 1;
    }
  }
  if (signal.votedUp === true) {
    polarity += 1.5;
    polarityHits += 1;
  } else if (signal.votedUp === false) {
    polarity -= 1.5;
    polarityHits += 1;
  }
  const valence = polarityHits
    ? Math.max(-1, Math.min(1, polarity / Math.max(2, polarityHits)))
    : 0;
  const emotions = Object.fromEntries(
    EMO_KEYS.map((emotion) => [
      emotion,
      countHits(normalized, SENTIMENT_LEXICON.emotions[emotion]) > 0 ? 1 : 0,
    ]),
  );
  const aspectMentions = Object.fromEntries(
    ASP_KEYS.map((aspect) => [
      aspect,
      countHits(normalized, SENTIMENT_LEXICON.aspects[aspect]),
    ]),
  );
  const arousal = clamp01(
    (countHits(normalized, SENTIMENT_LEXICON.arousal) + ((text || "").split("!").length - 1)) / 4,
  );
  return { valence, emotions, aspectMentions, arousal };
}

export function buildVector(features: readonly TextFeatures[]): number[] {
  const divisor = features.length || 1;
  const meanValence = features.reduce((sum, feature) => sum + feature.valence, 0) / divisor;
  const arousal = features.reduce((sum, feature) => sum + feature.arousal, 0) / divisor;
  const emotions = EMO_KEYS.map(
    (emotion) => features.reduce((sum, feature) => sum + feature.emotions[emotion], 0) / divisor,
  );
  const aspects = ASP_KEYS.map((aspect) => {
    let positive = 0;
    let negative = 0;
    let mentioned = 0;
    for (const feature of features) {
      if (!feature.aspectMentions[aspect]) continue;
      mentioned += 1;
      if (feature.valence > 0.05) positive += 1;
      else if (feature.valence < -0.05) negative += 1;
    }
    return mentioned ? clamp01(0.5 + (0.5 * (positive - negative)) / mentioned) : 0.5;
  });
  const positiveRatio = features.filter((feature) => feature.valence > 0.05).length / divisor;
  const volumeLog = clamp01(Math.log10(features.length + 1) / 4);
  return [
    clamp01((meanValence + 1) / 2),
    arousal,
    ...emotions,
    ...aspects,
    positiveRatio,
    volumeLog,
  ].map(round4);
}

export function textToVector(text: string): number[] {
  return buildVector([extractTextFeatures(text)]);
}

function assertSameDimension(left: readonly number[], right: readonly number[]): void {
  if (left.length !== right.length) {
    throw new Error(`vector dim mismatch: ${left.length} vs ${right.length}`);
  }
}

export function dot(left: readonly number[], right: readonly number[]): number {
  assertSameDimension(left, right);
  return left.reduce((sum, value, index) => sum + value * right[index], 0);
}

export function subtract(left: readonly number[], right: readonly number[]): number[] {
  assertSameDimension(left, right);
  return left.map((value, index) => value - right[index]);
}

export function norm(vector: readonly number[]): number {
  return Math.sqrt(dot(vector, vector));
}

export function scalarProjection(vector: readonly number[], onto: readonly number[]): number {
  const magnitude = norm(onto);
  return magnitude === 0 ? 0 : dot(vector, onto) / magnitude;
}

export function cosine(left: readonly number[], right: readonly number[]): number {
  const leftNorm = norm(left);
  const rightNorm = norm(right);
  return leftNorm === 0 || rightNorm === 0 ? 0 : dot(left, right) / (leftNorm * rightNorm);
}

export function weightedMean(vectors: readonly number[][], weights: readonly number[]): number[] {
  if (vectors.length !== weights.length) {
    throw new Error(`vector/weight count mismatch: ${vectors.length} vs ${weights.length}`);
  }
  if (vectors.length === 0) return new Array<number>(DIM).fill(0);
  for (const vector of vectors) {
    if (vector.length !== vectors[0].length) {
      throw new Error("weighted mean requires vectors with the same dimension");
    }
  }
  const weightSum = weights.reduce((sum, weight) => sum + weight, 0) || 1;
  return vectors[0].map((_, dimension) =>
    round4(vectors.reduce((sum, vector, index) => sum + vector[dimension] * weights[index], 0) / weightSum),
  );
}
