import { describe, expect, it } from "vitest";

import {
  DIM,
  VECTOR_SPEC,
  buildTargetVector,
  buildVector,
  computeDesignGap,
  extractTextFeatures,
  loadAffectVocabulary,
  scoreText,
  textToVector,
  weightedMean,
} from "../src/index.js";

describe("sentiment-core", () => {
  it("keeps the canonical fixed 20-dimensional vector", () => {
    expect(DIM).toBe(20);
    expect(VECTOR_SPEC).toHaveLength(20);
    expect(textToVector("最高に面白い!")).toHaveLength(20);
  });

  it("matches the original single-text feature/vector pipeline", () => {
    expect(textToVector("最高に面白い! story も beautiful")).toEqual(
      buildVector([extractTextFeatures("最高に面白い! story も beautiful")]),
    );
    expect(scoreText("最高に面白い!").valenceLabel).toBe("positive");
  });

  it("builds target and gap vectors without changing dimensions", () => {
    const target = buildTargetVector([
      { intended_valence: "positive", intended_emotions: ["joy"], intended_aspects: ["fun"] },
    ]);
    const current = new Array<number>(DIM).fill(0.5);
    expect(target).toHaveLength(DIM);
    expect(computeDesignGap(current, target)).toHaveLength(DIM);
  });

  it("computes deterministic weighted means", () => {
    const left = new Array<number>(DIM).fill(0);
    const right = new Array<number>(DIM).fill(1);
    expect(weightedMean([left, right], [1, 3])).toEqual(new Array<number>(DIM).fill(0.75));
  });

  it("ships the 24-entry controlled affect vocabulary", () => {
    const vocabulary = loadAffectVocabulary();
    expect(vocabulary).toHaveLength(24);
    expect(vocabulary.some((entry) => entry.key === "tension_release")).toBe(true);
  });
});
