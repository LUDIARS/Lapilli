# feature: mail-watch — Gmail Pub/Sub StreamingPull

`@ludiars/mail-watch` は Gmail Pub/Sub 通知を購読するだけの薄いパッケージであり、Gmail API やメール本文を扱わない。

## [id: SPEC-MAIL-WATCH-001] 成功した通知だけを ack する

`onNotification` が resolve した後にだけ Pub/Sub message を ack し、例外時は nack する。

## [id: SPEC-MAIL-WATCH-002] 終端エラーから再接続する

terminal error では subscription とその Pub/Sub client を close し、指数バックオフ後に再接続する。同じ接続から重複して届いた error は一度の再接続へまとめ、再接続回数を state に公開する。

## [id: SPEC-MAIL-WATCH-003] 稼働状態を公開する

state は接続状態、最終通知時刻、最終エラー、受信数、再接続数を含む。診断用の `onError` が例外を投げても、ack / nack および再接続の判断は変えない。

## [id: SPEC-MAIL-WATCH-004] 毒 payload を再配信しない

JSON または Gmail 通知として不正な payload は ack して破棄し、lastError に理由を残す。無限 nack ループにはしない。
