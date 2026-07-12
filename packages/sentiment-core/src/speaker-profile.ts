import { DIM, textToVector, weightedMean } from "./vector.js";

export interface SpeakerOpinions {
  speakerId: string;
  source?: string;
  opinions: Array<{ text: string; gameSlug: string | null; weight?: number }>;
}

export interface EvaluateSpeakerOptions {
  minOpinions?: number;
  polarityEps?: number;
  gameGapEps?: number;
}

export type SpeakerRejectReason = "too-few" | "all-positive" | "all-negative" | "no-game-gap";

export interface SpeakerCandidate {
  speakerId: string;
  source?: string;
  affect: number[];
  count: number;
  polarityBias: number;
  affectDispersion: number;
}

function euclideanDistance(left: readonly number[], right: readonly number[]): number {
  if (left.length !== right.length) throw new Error(`vector dim mismatch: ${left.length} vs ${right.length}`);
  return Math.sqrt(left.reduce((sum, value, index) => sum + (value - right[index]) ** 2, 0));
}

export function evaluateSpeakers(
  speakers: readonly SpeakerOpinions[],
  options: EvaluateSpeakerOptions = {},
): {
  candidates: SpeakerCandidate[];
  rejected: Array<{ speakerId: string; reason: SpeakerRejectReason; count: number }>;
} {
  const minOpinions = options.minOpinions ?? 10;
  const polarityEps = options.polarityEps ?? 0.05;
  const gameGapEps = options.gameGapEps ?? 0.05;
  const candidates: SpeakerCandidate[] = [];
  const rejected: Array<{ speakerId: string; reason: SpeakerRejectReason; count: number }> = [];
  for (const speaker of speakers) {
    const count = speaker.opinions.length;
    if (count < minOpinions) {
      rejected.push({ speakerId: speaker.speakerId, reason: "too-few", count });
      continue;
    }
    const byGame = new Map<string, { texts: string[]; weight: number }>();
    for (const opinion of speaker.opinions) {
      const key = opinion.gameSlug ?? "_none";
      const aggregate = byGame.get(key) ?? { texts: [], weight: 0 };
      aggregate.texts.push(opinion.text);
      aggregate.weight += opinion.weight ?? 1;
      byGame.set(key, aggregate);
    }
    const games = [...byGame.values()];
    if (games.length < 2) {
      rejected.push({ speakerId: speaker.speakerId, reason: "no-game-gap", count });
      continue;
    }
    const vectors = games.map((game) => textToVector(game.texts.join(" \n ")));
    const positive = vectors.filter((vector) => vector[0] > 0.5 + polarityEps).length;
    const negative = vectors.filter((vector) => vector[0] < 0.5 - polarityEps).length;
    if (positive === vectors.length) {
      rejected.push({ speakerId: speaker.speakerId, reason: "all-positive", count });
      continue;
    }
    if (negative === vectors.length) {
      rejected.push({ speakerId: speaker.speakerId, reason: "all-negative", count });
      continue;
    }
    let maximumGap = 0;
    let pairDistanceSum = 0;
    let pairCount = 0;
    for (let left = 0; left < vectors.length; left += 1) {
      for (let right = left + 1; right < vectors.length; right += 1) {
        const distance = euclideanDistance(vectors[left], vectors[right]);
        maximumGap = Math.max(maximumGap, distance);
        pairDistanceSum += distance;
        pairCount += 1;
      }
    }
    if (maximumGap <= gameGapEps) {
      rejected.push({ speakerId: speaker.speakerId, reason: "no-game-gap", count });
      continue;
    }
    const affect = weightedMean(vectors, games.map((game) => game.weight));
    if (affect.length !== DIM) throw new Error(`affect vector dim must be ${DIM}`);
    candidates.push({
      speakerId: speaker.speakerId,
      source: speaker.source,
      affect,
      count,
      polarityBias: Number((Math.abs(positive - negative) / vectors.length).toFixed(4)),
      affectDispersion: pairCount ? Number((pairDistanceSum / pairCount).toFixed(4)) : 0,
    });
  }
  return { candidates, rejected };
}
