import { ASP_KEYS, EMO_KEYS, extractTextFeatures } from "./vector.js";

export const EMOTIONS = EMO_KEYS;
export const ASPECTS = ASP_KEYS;

export type ValenceLabel = "positive" | "negative" | "neutral";

export interface AffectScore {
  valence: number;
  valenceLabel: ValenceLabel;
  emotions: string[];
  aspects: Record<string, number>;
  aspectsHit: string[];
  arousal: number;
}

export function scoreText(text: string): AffectScore {
  const features = extractTextFeatures(text);
  const valenceLabel: ValenceLabel =
    features.valence > 0.05 ? "positive" : features.valence < -0.05 ? "negative" : "neutral";
  const emotions = EMO_KEYS.filter((emotion) => features.emotions[emotion] > 0);
  const aspects = { ...features.aspectMentions };
  return {
    valence: features.valence,
    valenceLabel,
    emotions,
    aspects,
    aspectsHit: ASP_KEYS.filter((aspect) => aspects[aspect] > 0),
    arousal: features.arousal,
  };
}
