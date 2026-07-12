import lexiconJson from "../data/lexicon.json" with { type: "json" };

export interface SentimentLexicon {
  polarity: Record<string, number>;
  emotions: Record<string, string[]>;
  aspects: Record<string, string[]>;
  arousal: string[];
}

export const SENTIMENT_LEXICON = lexiconJson as SentimentLexicon;
