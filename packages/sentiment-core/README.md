# @ludiars/sentiment-core

Discutere の固定 20 次元感情空間を、Discutere と CommonJS の Voluptas から同じ実装で使うための依存ゼロランタイムパッケージです。感情次元の正本は `VECTOR_SPEC` で、順序・意味・0..1 の範囲を変更しません。

公開 API:

- `DIM`, `VECTOR_SPEC`, `textToVector`, `dot`, `subtract`, `norm`, `cosine`, `scalarProjection`
- `scoreText`
- `cascadeSentiment`（LLM client は注入）
- `loadAffectVocabulary`
- `buildTargetVector`, `computeDesignGap`
- `evaluateSpeakers`, `weightedMean`

```js
const { textToVector, scoreText } = require("@ludiars/sentiment-core");

const vector = textToVector("最高に面白い!");
const score = scoreText("最高に面白い!");
```

ESM では同じ名前を `import` できます。ビルドは ESM (`dist/index.js`) と CommonJS (`dist/index.cjs`) を同時に生成します。
