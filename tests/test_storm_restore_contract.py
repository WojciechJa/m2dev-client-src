from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class StormRestoreContractTest(unittest.TestCase):
    def test_snapshot_keeps_exact_particle_count(self) -> None:
        source = (REPO_ROOT / "src" / "GameLib" / "StormEnvironment.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "m_dwBaseRainParticleCount = m_pRainEnvironment->GetParticleCount();",
            source,
        )
        self.assertIn(
            "m_pRainEnvironment->SetParticleCount(m_dwBaseRainParticleCount);",
            source,
        )

    def test_diagnostic_forces_particle_and_wind_mutations_before_disable(self) -> None:
        source = (REPO_ROOT / "src" / "GameLib" / "StormEnvironment.cpp").read_text(
            encoding="utf-8"
        )
        enable = source.index("\tEnable();", source.index("RunWeatherRestoreDiagnostic"))
        particle_mutation = source.index(
            "m_pRainEnvironment->SetParticleCount(dwDiagnosticParticleCount);", enable
        )
        wind_mutation = source.index(
            "m_pRainEnvironment->SetWindVector(v3DiagnosticWind);", particle_mutation
        )
        disable = source.index("\tDisable();", wind_mutation)
        self.assertLess(enable, particle_mutation)
        self.assertLess(particle_mutation, wind_mutation)
        self.assertLess(wind_mutation, disable)


if __name__ == "__main__":
    unittest.main()
