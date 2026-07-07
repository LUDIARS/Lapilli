import { describe, expect, it } from "vitest";
import { analyzeHome, extractJson } from "./analysis.js";
import type { LibrarySource } from "./types.js";

const source: LibrarySource = {
  id: "mem:test",
  kind: "memory",
  sourceKind: "central-memory",
  label: "test",
  rootDir: "/tmp/memory",
  blocks: [
    {
      id: "mem:test::a.md",
      sourceId: "mem:test",
      kind: "memory",
      name: "a.md",
      title: "A",
      description: "first",
      relPath: "a.md",
      size_bytes: 10,
      line_count: 1,
      mtime: 1,
      flags: {},
    },
  ],
};

describe("extractJson", () => {
  it("accepts fenced JSON", () => {
    expect(extractJson("```json\n{\"ok\":true}\n```")).toEqual({ ok: true });
  });
});

describe("analyzeHome", () => {
  it("normalizes runner suggestions to block ids and drops unknown names", async () => {
    const result = await analyzeHome(source, {
      runner: async () => ({
        ok: true,
        stdout: JSON.stringify({
          suggestions: [
            { kind: "merge", names: ["a.md"], message: "merge candidate" },
            { kind: "archive", names: ["missing.md"], message: "archive candidate" },
          ],
        }),
        stderr: "",
      }),
    });

    expect(result.suggestions).toEqual([
      { kind: "merge", blockIds: ["mem:test::a.md"], message: "merge candidate" },
    ]);
  });

  it("reports explicit disabled mode without calling a runner", async () => {
    const result = await analyzeHome(source, { disabled: true });
    expect(result).toMatchObject({ disabled: true, suggestions: [] });
  });
});
