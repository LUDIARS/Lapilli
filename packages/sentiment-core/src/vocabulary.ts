import vocabularyJson from "../data/vocabulary.json" with { type: "json" };

export type AffectValence = "positive" | "negative" | "ambivalent";

export interface AffectEntry {
  key: string;
  label_ja: string;
  valence: AffectValence;
  mda?: string;
  description: string;
}

interface AffectVocabularyDocument {
  version: string;
  affects: AffectEntry[];
}

const VOCABULARY = vocabularyJson as AffectVocabularyDocument;

export function loadAffectVocabulary(): AffectEntry[] {
  return VOCABULARY.affects.map((entry) => ({ ...entry }));
}

export function affectVocabularyVersion(): string {
  return VOCABULARY.version;
}
