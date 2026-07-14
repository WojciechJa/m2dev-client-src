# First-party checks

Run the lightweight repository checks without building the game client:

```powershell
python -m unittest discover -s tests -v
```

These checks protect repository hygiene and CI branch coverage while the DX11
migration is being split into reviewed commits. Native renderer, packet and
runtime fixtures belong here as they are extracted from the current worktree.

Validate a real strict-native login log with:

```powershell
python tools/verify_dx11_native_ui_log.py H:\m2dev-client\m2dev-client\log\syserr.txt
```

The validator requires native UI/text submissions, a successful present and a
zero-failure widget heartbeat while rejecting legacy DX9-device activation and
critical native-present failures.
