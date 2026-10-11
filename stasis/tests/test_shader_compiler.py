"""SPDX-License-Identifier: MIT. Copyright (c) Clay Mullis."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "compiler", Path(__file__).resolve().parents[1] / "tools/compile_shaders.py")
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)


class NativeShaderContract(unittest.TestCase):
    def test_rejects_deployed_v017_chain_and_particle_instructions(self):
        # Actual instructions from the deployed shaders and Citron's rejected opcode.
        for stage, instruction in (("chainvert", "IMAD.HI R1, R0, R1, R0"),
                                   ("particlesvert", "IMAD.HI R5, R2, R5, R2")):
            with self.assertRaisesRegex(ValueError, r"IMAD \(reg\)"):
                compiler.validate_gpu_assembly(stage, f"/*0098*/ {instruction} ;\n/*0e90*/ EXIT ;")

    def test_rejects_deployed_v014_texture_query(self):
        # Actual instructions from our failed v0.1.4 shader: query 9, fetch A.
        broken = """
        /*02b0*/ TXQ R0, R0, TEX_HEADER_DIMENSION, 0x9, 0x3 ;
        /*0328*/ TLDS.LZ RZ, R0, R0, R1, 0xa, 2D, R ;
        /*7400*/ EXIT ;
        """
        with self.assertRaisesRegex(ValueError, "incorrect NVN handle stride"):
            compiler.validate_gpu_assembly("frag", broken)

    def test_explicit_dimensions_need_no_query(self):
        compiler.validate_gpu_assembly("frag", """
        /*02b0*/ MOV R0, c[0x3][0x340] ;
        /*0328*/ TLDS.LZ RZ, R0, R0, R1, 0xa, 2D, R ;
        /*7400*/ EXIT ;
        """)

    def test_previous_uniform_layout_is_rejected(self):
        declarations = compiler.EXPECTED_BINDINGS["frag"]
        compiler.validate_bindings("frag", "\n".join(declarations))
        stale = "\n".join(declarations).replace("0..152", "0..52")
        with self.assertRaisesRegex(ValueError, "unexpected bindings"):
            compiler.validate_bindings("frag", stale)

    def test_geometry_requires_ordinary_vertex_path(self):
        for stage in ("chainvert", "particlesvert"):
            declarations = "\n".join(compiler.EXPECTED_BINDINGS[stage])
            compiler.validate_bindings(stage, declarations + "\nDCL SV[0], VERTEXID")
            with self.assertRaisesRegex(ValueError, "INSTANCEID"):
                compiler.validate_bindings(stage, declarations + "\nDCL SV[1], INSTANCEID")


if __name__ == "__main__":
    unittest.main()
