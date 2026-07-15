from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def git(*args: str) -> str:
    return subprocess.check_output(
        ["git", *args],
        cwd=REPO_ROOT,
        text=True,
        encoding="utf-8",
    )


class RepositoryContractsTest(unittest.TestCase):
    def test_terrain_coverage_setting_controls_quadtree_culling(self) -> None:
        renderer = (
            REPO_ROOT / "src" / "GameLib" / "MapOutdoorRenderDX11.cpp"
        ).read_text(encoding="utf-8")
        normalized = "".join(renderer.split())

        self.assertIn(
            "__RenderTerrain_RecurseRenderQuadTree("
            "m_pRootNode,!DX11RuntimeConfig::kForceFullTerrainCoverage);",
            normalized,
        )

    def test_ci_covers_active_development_branch_and_pull_requests(self) -> None:
        workflow = (REPO_ROOT / ".github" / "workflows" / "main.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("pull_request:", workflow)
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("- dx11-world-render", workflow)

    def test_local_artifacts_are_not_tracked(self) -> None:
        tracked = set(git("ls-files").splitlines())
        forbidden_files = {
            ".claude/settings.local.json",
            "nul",
            "extern/nul",
            "src/EterLib/__codex_write_test.tmp",
            "src/EterLib/gr_async_append.txt",
            "tools/WorldEditor-Renewal-main.zip",
        }
        forbidden_prefixes = (
            "src_BAK_DO_NOT_TOUCH/",
            "tools/WorldEditor-Renewal-main/",
            "src/WorldEditor/Release/",
        )

        violations = sorted(
            path
            for path in tracked
            if path in forbidden_files or path.startswith(forbidden_prefixes)
        )
        self.assertEqual([], violations)

    def test_gitlinks_require_an_explicit_submodule_declaration(self) -> None:
        staged_entries = git("ls-files", "--stage").splitlines()
        gitlinks = [line.rsplit("\t", 1)[-1] for line in staged_entries if line.startswith("160000 ")]

        gitmodules = REPO_ROOT / ".gitmodules"
        declared = gitmodules.read_text(encoding="utf-8") if gitmodules.exists() else ""
        undeclared = [path for path in gitlinks if f"path = {path}" not in declared]
        self.assertEqual([], undeclared)

    def test_directxtk_is_pinned_and_available(self) -> None:
        gitmodules = (REPO_ROOT / ".gitmodules").read_text(encoding="utf-8")
        self.assertIn("path = extern/third_party/DirectXTK", gitmodules)
        self.assertTrue(
            (REPO_ROOT / "extern" / "third_party" / "DirectXTK" / "Inc" / "SimpleMath.h").is_file()
        )

    def test_imgui_is_pinned_and_available(self) -> None:
        gitmodules = (REPO_ROOT / ".gitmodules").read_text(encoding="utf-8")
        self.assertIn("path = vendor/imgui", gitmodules)
        self.assertTrue((REPO_ROOT / "vendor" / "imgui" / "imgui.h").is_file())
        self.assertTrue(
            (REPO_ROOT / "vendor" / "imgui" / "backends" / "imgui_impl_dx11.cpp").is_file()
        )


if __name__ == "__main__":
    unittest.main()
