---
task: mail-realtime-pubsub
project: Lapilli
kind: 機能追加
created: 2026-09-04
---
# Gmail リアルタイム監視用の共有パッケージを追加する

## 目的

Gmail の history/watch API と Pub/Sub StreamingPull 購読を、取得と購読を分離した共有パッケージとして提供する。

## 完了条件

- `@ludiars/mail-inbox` が履歴差分、watch 登録・解除、現在の history ID を提供する。
- `@ludiars/mail-watch` が通知成功後だけ ack し、終端エラーから再接続し、状態を公開する。
- 404 の履歴失効とページング、ack/nack、再接続、毒 payload をテストで網羅する。
