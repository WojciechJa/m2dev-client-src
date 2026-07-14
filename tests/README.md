# First-party checks

Run the lightweight repository checks without building the game client:

```powershell
python -m unittest discover -s tests -v
```

These checks protect repository hygiene and CI branch coverage while the DX11
migration is being split into reviewed commits. Native renderer, packet and
runtime fixtures belong here as they are extracted from the current worktree.
