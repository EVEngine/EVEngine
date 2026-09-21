# Agent repair relay

This local process polls GitHub for opted-in EVEngine PR repair requests and dispatches them to an explicitly configured local agent. GitHub Actions only creates metadata; it never starts a local machine and this project does not use a self-hosted runner.

## Repository setup

Merge the `Agent repair router` workflow into the default branch, then create these PR labels:

- `agent:auto-fix` — explicit opt-in
- exactly one `agent:owner:<name>` — the relay owner
- exactly one of `agent:provider:codex` or `agent:provider:deepseek`

The router creates its queue labels automatically. Only open PRs whose head repository is exactly `EVEngine/EVEngine` are eligible. Fork PRs are always rejected. A failed/timed-out `CI` workflow run or a submitted changes-requested review creates or updates one machine-readable state comment. Adding the opt-in label after CI failed also re-evaluates the latest `CI` run for the current head. Closing, opting out, or pushing a new head cancels stale pending work. The default limit is three attempts; set the repository variable `AGENT_REPAIR_MAX_ATTEMPTS` to change it.

## Local setup

1. Copy `config.example.json` to a private path outside the repository and set `owner`, `workspace`, and a unique `relay_id`.
2. Create a fine-grained token with read access to Actions/metadata/code and read-write access to Issues and Pull requests. Put it in `GH_TOKEN` (or change `token_env`). Never commit it.
3. Configure each provider command as a JSON array. The relay never uses a shell. It sends the repair prompt on stdin unless an argument contains `{prompt_file}`, in which case that placeholder is replaced with a temporary UTF-8 prompt file.

Provider CLI syntax changes over time, so the example intentionally leaves both commands empty. Example shapes (verify against the installed CLI before enabling) are:

```json
"codex": { "command": ["codex", "<non-interactive-subcommand>", "{prompt_file}"] }
"deepseek": { "command": ["deepseek", "<non-interactive-subcommand>", "{prompt_file}"] }
```

Test one poll without claiming work, changing GitHub state, or launching a provider:

```powershell
python tools/agent_relay/agent_relay.py --config C:\private\agent-relay.json --once --dry-run
```

Run continuously (30-second default interval):

```powershell
python tools/agent_relay/agent_relay.py --config C:\private\agent-relay.json
```

The relay verifies same-repository ownership and the exact head SHA both before and after its optimistic claim. It logs queue decisions and provider exit status. A provider is responsible for fetching the named PR head, rechecking the SHA before edits/push, making a narrow repair, and pushing to the existing branch; it must not merge.
