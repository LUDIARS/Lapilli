import { ASP_KEYS, DIM, EMO_KEYS, VECTOR_SPEC, subtract } from "./vector.js";

export interface MechanicIntent {
  intended_aspects?: string[];
  intended_emotions?: string[];
  intended_valence?: string;
}

const ASPECT_INDEX = new Map(ASP_KEYS.map((aspect, index) => [aspect, 2 + EMO_KEYS.length + index]));
const EMOTION_INDEX = new Map(EMO_KEYS.map((emotion, index) => [emotion, 2 + index]));
const VALENCE_INDEX = VECTOR_SPEC.indexOf("emo.valence");

export function buildTargetVector(mechanics: readonly MechanicIntent[]): number[] {
  const target = new Array<number>(DIM).fill(0.5);
  for (const mechanic of mechanics) {
    for (const aspect of mechanic.intended_aspects ?? []) {
      const index = ASPECT_INDEX.get(aspect);
      if (index !== undefined) target[index] = 1;
    }
    for (const emotion of mechanic.intended_emotions ?? []) {
      const index = EMOTION_INDEX.get(emotion);
      if (index !== undefined) target[index] = 1;
    }
  }
  const valences = mechanics
    .map((mechanic) => mechanic.intended_valence)
    .filter((value): value is string => typeof value === "string");
  if (valences.length === 0) {
    target[VALENCE_INDEX] = 1;
  } else {
    const sum = valences.reduce(
      (total, value) => total + (value === "positive" ? 1 : value === "negative" ? -1 : 0),
      0,
    );
    target[VALENCE_INDEX] = (sum / valences.length + 1) / 2;
  }
  return target;
}

export function computeDesignGap(current: readonly number[], target: readonly number[]): number[] {
  return subtract(target, current);
}
