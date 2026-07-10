# feature: blackbox — 顔ログイン accept 判定のブースト (`ludellus.face_login_accept`)

`@ludiars/blackbox` を Ludellus の位置認証ログイン (顔認識) に適用する際の domain 設計。
利用側の全体設計は `Ludellus/spec/feature/location-face-auth.md` §3/§5-4 が正本、
本書は blackbox 側の実装義務 3 点 (features 化 / LlmFallback / レビュー UI) の詳細を担う。

正本ソース: `packages/blackbox/DESIGN.md` (エンジン本体) / 本書は §7 採用マップへの追記。

---

## 0. なぜ blackbox を使うか

決定的な顔照合 (`IdentityMatcher` の cosine 類似度) は `accept_threshold` /
`margin` という固定閾値で accept/reject を決めるが、実運用では照明条件・カメラ個体差・
時間帯でスコア分布がずれ、固定閾値のままだと現場でチューニングし続ける羽目になる。
blackbox の「LLM 判断 → ルール昇格 → LLM 卒業」ループを、**この閾値付近の曖昧域だけ**に
適用し、運用データから自動でルールを育てる。

## 1. 境界: 生体データを渡さない

- features に入れて良いのは **決定的照合が既に計算した派生スカラー値のみ**。
  埋め込みベクトル・顔画像・顔ランドマークは一切渡さない。
- 生体データの単一情報源は Cernere (`Ludellus/spec/feature/face-identity.md` §Cernere 連携)
  のまま変わらない。blackbox の SQLite/JSON ストアには一切生体データが乗らないことを
  この domain のレビュー時に確認する (テストで features のキー集合を固定 assert する)。

## 2. features → output

| features key | 型 | 意味 |
|---|---|---|
| `cosineBest` | number | roster 内最良候補との cosine 類似度 |
| `cosineMargin` | number | 最良候補と次点候補の差 (小さいほど曖昧) |
| `livenessScore` | number | anti-spoofing モデルの live 確信度 |
| `embeddingQuality` | number | blur/pose 由来の品質スコア (低品質は reject 側に倒したい) |
| `voteCount` | number | `IdentityResolver` の時間投票数 (少ないほど不安定) |
| `historicalAcceptRate` | number | 同一 user の直近 N 回の accept 率 (急変は疑わしい) |

出力 `output`: `"accept" | "reject" | "manual_review"`。

`when` (Condition) はこれらの数値レンジのみで組み立てる。roster の user_id や場所 ID を
条件に含めない (個人を狙い撃ちしたルールを育てない)。

## 3. LlmFallback

- 決定的照合が **明白域** (`cosineBest` が accept 閾値を大きく上回る、または reject 閾値を
  大きく下回る) と判定した場合は blackbox を呼ばない。**曖昧域のみ** `decide()` に通す。
- LlmFallback は「この特徴量の組み合わせで人間の運用者ならどう判断するか」を一撃で
  返すプロンプトとし、`proposedRule` (candidate 提案) を含める (DESIGN.md §1 の形式に従う)。

## 4. セキュリティドメイン固有のガードレール (DESIGN.md への追加要件)

通常ドメイン (天気判定・科目コード割当等) と違い、本 domain は **falseAccept のコストが
非対称に高い** (誤って他人をログインさせてしまう)。既定の `EngineOptions` をそのまま
使わず、以下を要件化する:

1. **`autoPromote` 方向の非対称化** — output が `"accept"` へ向かうルールの `auto` 昇格は
   既定で無効化し、`trial` 止まり (毎回 `pending_review`) に固定するオプションを
   `EngineOptions` に追加する (例 `autoPromoteBlocklist: ["accept"]` のような output 値
   単位のブロック)。`"reject"` / `"manual_review"` 方向は通常通り自動昇格を許す。
2. **影評価汚染への耐性** — 境界事例を繰り返し送って `shadowAgreements` を稼ぎ accept
   ルールを育てさせる攻撃 (毒盛り) を想定し、(1) の非対称ガードで accept 系は auto に
   到達できないようにすることが主防御。合わせて `shadowPromote` 閾値をこの domain だけ
   引き上げる運用値を推奨する。
3. **撤回済み指紋の再提案ブロック** (DESIGN.md §1 の `retired` 挙動) をそのまま活用し、
   一度 NG が積まれて撤回された「緩め提案」の再送を機械的に弾く。

## 5. 卒業の意味

`stats("ludellus.face_login_accept").ruleCoverage` が 1.0 に近づくのは「reject/manual_review
方向の判断がルール化されて LLM 呼び出しが要らなくなった」ことを意味する。accept 方向は
(4)-1 のガードにより `trial` に留まり続けるため、**accept は常に人間 (運用者) の
review を経由する設計を維持する** — これが本 domain における「卒業しない部分を残す」
意図的な設計判断である。

## 関連

- `packages/blackbox/DESIGN.md` (エンジン本体、§1 ライフサイクル・§7 採用マップ)
- `Ludellus/spec/feature/location-face-auth.md` (呼び出し元・脆弱性レビュー全体)
- `Ludellus/spec/feature/face-identity.md` (IdentityMatcher/Resolver の決定的照合コア)
