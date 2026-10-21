import { selectAvailableModel } from './availability.js';

/** Provider-neutral work only. The caller's executor owns CLI capabilities and deadlines. */
export async function runAvailableOneShot(prompt, options) {
  if (typeof prompt !== 'string') throw new Error('One-shot prompt must be text');
  const { signal, executors, readAvailability, requested } = options;
  signal?.throwIfAborted();
  if (!requested && typeof readAvailability !== 'function') throw new Error('One-shot availability reader required');
  const availability = requested ? undefined : await readAvailability({ signal });
  signal?.throwIfAborted();
  const selection = selectAvailableModel({ ...options, availability });
  const execute = executors?.[selection.provider];
  if (typeof execute !== 'function') throw new Error(`One-shot executor missing:${selection.provider}`);
  // Never replay on failure: the first executor may have produced an external effect.
  const result = await execute(prompt, { ...selection, signal });
  return { selection, result };
}
