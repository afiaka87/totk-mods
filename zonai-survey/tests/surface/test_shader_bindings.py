# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest
import struct
import sys

spec = importlib.util.spec_from_file_location(
    "compile_shaders", Path(__file__).resolve().parents[2] / "tools/compile_shaders.py")
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)
memory_spec = importlib.util.spec_from_file_location(
    "profile_memory", Path(__file__).resolve().parents[2] / "tools/profile_memory.py")
memory = importlib.util.module_from_spec(memory_spec)
memory_spec.loader.exec_module(memory)
sys.modules["profile_memory"] = memory
budget_spec = importlib.util.spec_from_file_location(
    "surface_budget", Path(__file__).resolve().parents[2] / "tools/check_surface_budget.py")
budget = importlib.util.module_from_spec(budget_spec)
budget_spec.loader.exec_module(budget)

class MemoryProfileTests(unittest.TestCase):
    def test_depth_budget_refuses_growth_and_uncompressed_or_diagnostic_data(self):
        budget.validate({"mapped_bytes": 1048576}, "0000 r zonai_survey::glyphs::kPlacementBytes")
        with self.assertRaises(ValueError):
            budget.validate({"mapped_bytes": 1048577}, "")
        for symbol in ("zonai_survey::glyphs::kPlacements", "survey_fidelity::shaders::calibrationCode",
                       "survey_fidelity::trace::begin()"):
            with self.subTest(symbol=symbol), self.assertRaises(ValueError):
                budget.validate({"mapped_bytes": 600000}, "0000 r "+symbol)
        budget.validate({"mapped_bytes": 600000}, "0000 r zonai_survey::glyphs::kPlacements", compact=False)

    def test_mapped_extent_includes_zero_fill_bss_and_alignment(self):
        header = bytearray(256)
        header[:4] = b"NSO0"
        for at, address, size in ((0x10, 0, 4096), (0x20, 8192, 100), (0x30, 12288, 20)):
            struct.pack_into("<III", header, at, 0, address, size)
        struct.pack_into("<I", header, 0x3C, 8192)
        profile = memory.nso_profile(header)
        self.assertEqual(profile["mapped_bytes"], 24576)
        self.assertEqual(profile["bss_bytes"], 8192)
        with self.assertRaises(ValueError):
            memory.nso_profile(b"bad")

    def test_failed_queries_remain_explicit_in_boot_samples(self):
        sample = memory.boot_samples("[survey-fidelity] MEMORY phase=install total=99 used=0 peak=0 system=1/2 rc=0,d401,0,0\n")[0]
        self.assertEqual(sample["results"], [0, 0xD401, 0, 0])
        self.assertEqual(sample["phase"], "install")

class ShaderBindingsTests(unittest.TestCase):
    def test_fixed_regular_font_and_draw_on_glass_have_no_runtime_selector_or_depth(self):
        root = Path(__file__).resolve().parents[2]
        renderer = (root / 'src/presentation/AtlasRenderer.cpp').read_text(encoding='utf-8')
        gpu = (root / 'src/surface/AtlasGpu.cpp').read_text(encoding='utf-8')
        module = (root / 'src/program/modules/zonai-survey/Module.cpp').read_text(encoding='utf-8')
        baker = (root / 'tools/bake_label_atlas.py').read_text(encoding='utf-8')
        self.assertIn("FONT_FILES = ('RodinM.bfotf',)", baker)
        self.assertNotIn('cycleAtlasFont', module+renderer)
        self.assertNotIn('fontRight', module)
        self.assertNotIn('ATLAS20', renderer)
        self.assertNotIn('448+i*24', renderer)
        self.assertNotIn('SURVEY_STARTUP_DIAGNOSTIC', renderer)
        for removed in ('samplerBindings', 'depthSlot', 'labelDepth'):
            self.assertNotIn(removed, gpu+renderer)
        self.assertIn('state[0]=state[1]=0', gpu)

    def test_atlas_shaders_use_only_their_own_uniforms(self):
        for shader, block in (("labelVert", "DCL CONST[2][0..255]"),
                              ("labelFrag", "DCL CONST[2][0..3391]")):
            compiler.validate_bindings(shader, block)
            for extra in ("DCL SAMP[0]", "DCL IMAGE[0]", "DCL CONST[3][0]", "DCL BUFFER[0]"):
                with self.assertRaises(ValueError):
                    compiler.validate_bindings(shader, block+'\n'+extra)
        for symbol in ("lotuskit::TextWriter::createFrameHeap()", "sead::DebugFontMgrNvn::create()", "openSurveyAsset"):
            with self.assertRaises(ValueError):
                budget.validate({"mapped_bytes": 800000}, symbol, atlas=True)

    def test_constrained_range_is_snapshotted_and_clips_surface_and_grass(self):
        root = Path(__file__).resolve().parents[2]
        shader = (root / "shaders/diagnostic.frag").read_text(encoding="utf-8")
        native = (root / "src/surface/FidelityPlayground.cpp").read_text(encoding="utf-8")
        items = (root / "src/feature/GlyphController.cpp").read_text(encoding="utf-8")
        self.assertIn("g_pulseRange = zonai_survey::options::nextRange()", native)
        self.assertIn("uniforms.settings[1] = pulseRange", native)
        self.assertIn("range_ = options::nextRange()", items)
        self.assertIn("originX_, originZ_, scanRange()", items)
        self.assertIn("pure::withinSurveyRange(px-originX_, pz-originZ_, scanRange())", items)
        self.assertIn("#if SF_CONSTRAINED\n        maxRadius=settings.y;", shader)
        for prefix in ("color*=", "detail*="):
            self.assertIn(prefix+"1.0-smoothstep(settings.y-min(8.0,settings.y*0.05),settings.y,", shader)

    def test_cooldown_blocks_before_start_and_only_arms_after_success(self):
        root = Path(__file__).resolve().parents[2]
        module = (root / "src/program/modules/zonai-survey/Module.cpp").read_text(encoding="utf-8")
        trigger = module[module.index("pure::ScanVerdict triggerSurvey() {"):]
        self.assertLess(trigger.index("g_cooldown.remaining(now)"), trigger.index("g_scan.trigger()"))
        self.assertLess(trigger.index("if (verdict != pure::ScanVerdict::Accepted) return verdict;"), trigger.index("g_cooldown.start("))
        self.assertIn("if (verdict == zonai_survey::pure::ScanVerdict::CoolingDown) return;", module)
        self.assertIn("if (!SURVEY_TUNING || frame.snapshot().freshSampleCount)", module)
        self.assertNotIn("constexpr std::uint64_t left", module)
        self.assertNotIn("overlay::", module)
        self.assertNotIn("#if SURVEY_CONSTRAINED", trigger)
        self.assertIn("options::cooldownSeconds()", trigger)
        controller = (root / "src/surface/FidelityScanController.cpp").read_text(encoding="utf-8")
        self.assertNotIn("ScanVerdict::AlreadyPulsing", controller)

    def test_depth_completion_has_no_repeat_reminder_and_keeps_updates(self):
        module = (Path(__file__).resolve().parents[2] / "src/program/modules/zonai-survey/Module.cpp").read_text(encoding="utf-8")
        self.assertNotIn("ZL+Up to scan again", module)
        self.assertNotIn('showBanner("Surface scan complete"', module)
        self.assertIn("g_scan.tick();", module)
        self.assertIn("g_glyphs.tick();", module)
        self.assertNotIn("buildReachText(g_scan", module)

    def test_imprint_adapter_uses_forward_rows_and_blue_detail_only(self):
        root = Path(__file__).resolve().parents[2]
        shader = (root / "shaders/diagnostic.frag").read_text(encoding="utf-8")
        native = (root / "src/surface/FidelityPlayground.cpp").read_text(encoding="utf-8")
        self.assertIn("sfForward(p.x,p.z,scanHeading.x,scanHeading.y)", shader)
        self.assertIn("sfGridConfidence(plane.score)", shader)
        self.assertNotIn("imprintAt(world,rawDx", shader)
        self.assertIn("uniforms.scanHeading[2] = scanConeCosHalf()", native)
        self.assertIn("-1.f : imprintFront(seconds)", native)

    def test_item_discovery_and_depth_renderer_share_the_cone_constant(self):
        root = Path(__file__).resolve().parents[2]
        items = (root / "src/feature/GlyphController.cpp").read_text(encoding="utf-8")
        policy = (root / "src/surface/FidelityPolicy.hpp").read_text(encoding="utf-8")
        for source in (items, policy):
            self.assertRegex(source, r"pure::kSurveyWidthRadians\s*\*\s*0\.5f")

    def test_temporal_work_gate_precedes_fit_without_removing_grass_detail(self):
        shader = (Path(__file__).resolve().parents[2] / "shaders/diagnostic.frag").read_text(encoding="utf-8")
        self.assertLess(shader.index("if (!mayDraw)"), shader.index("SfPlane plane=sfFit"))
        self.assertIn("min(rawForward,min(nearForward,farForward))", shader)
        self.assertIn("max(rawForward,max(nearForward,farForward))", shader)
        self.assertIn("radiance=max(radiance,vec3(0.10,0.78,1.0)*detail)", shader)

    def test_scan_has_no_player_exclusion(self):
        root = Path(__file__).resolve().parents[2]
        for path in ("shaders/diagnostic.frag", "shaders/aesthetic_math.inl", "src/surface/FidelityPlayground.cpp"):
            source = (root / path).read_text(encoding="utf-8")
            for removed in ("sfPlayerMask", "sfRayPlayerMask", "playerPosition", "g_cachedPlayer", "updatePlayer"):
                with self.subTest(path=path, removed=removed):
                    self.assertNotIn(removed, source)

    def test_resource_free_stages(self):
        for name in ("vert", "calibration"):
            compiler.validate_bindings(name, "DCL IN[0]\nDCL OUT[0]\nIMM[0] FLT32 {0.3}")

    def test_calibration_and_vertex_reject_each_external_resource(self):
        for name in ("vert", "calibration"):
            for resource in ("CONST", "SAMP", "SVIEW", "IMAGE", "BUFFER"):
                with self.subTest(name=name, resource=resource), self.assertRaises(ValueError):
                    compiler.validate_bindings(name, f"  DCL {resource}[0]\n")

    def test_depth_requires_both_bindings(self):
        block, sampler = "DCL CONST[2][0..13]", "DCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT"
        compiler.validate_bindings("frag", block + "\n" + sampler)
        for missing in (block, sampler, "DCL CONST[1][0..9]\n" + sampler):
            with self.subTest(ir=missing), self.assertRaises(ValueError):
                compiler.validate_bindings("frag", missing)

    def test_depth_rejects_hidden_or_extra_resources(self):
        valid = "DCL CONST[2][0..13]\nDCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\n"
        for extra in ("DCL CONST[0][0]", "DCL SAMP[1]", "DCL SVIEW[1], 2D, FLOAT",
                      "DCL IMAGE[0]", "DCL BUFFER[0]", "DCL SAMP[0]"):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                compiler.validate_bindings("frag", valid + extra)

    def test_unknown_shader_is_rejected(self):
        with self.assertRaises(ValueError):
            compiler.validate_bindings("typo", "")

    def test_isolation_shaders_reject_cross_dependencies(self):
        block = "DCL CONST[2][0]"
        texture = "DCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT"
        compiler.validate_bindings("uniformCheck", block)
        compiler.validate_bindings("rawDepth", texture)
        for name in ("uniformCheck", "rawDepth"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                compiler.validate_bindings(name, block + "\n" + texture)

class GpuAssemblyTests(unittest.TestCase):
    def test_atlas_rejects_imad_but_accepts_supported_low_product(self):
        for name in ('labelVert', 'labelFrag'):
            for instruction in ('IMAD', 'IMAD.HI', '@P0 IMAD.HI', '@!P1 IMAD'):
                with self.subTest(shader=name, instruction=instruction), self.assertRaisesRegex(ValueError, 'IMAD'):
                    compiler.validate_gpu_assembly(name, f'/*0088*/ {instruction} R1, R0, R1, R0;\n/*0090*/ EXIT;')
        compiler.validate_gpu_assembly('labelVert', '/*0088*/ XMAD R1, R0, R1, RZ;\n/*0090*/ EXIT;')

    def test_bounded_vertex_indexing_matches_every_vertex_and_partial_batch(self):
        root = Path(__file__).resolve().parents[2]
        vertex = (root/'shaders/labels.vert').read_text(encoding='utf-8')
        native = (root/'src/surface/AtlasGpu.cpp').read_text(encoding='utf-8')
        layout = (root/'src/pure/AtlasLayout.hpp').read_text(encoding='utf-8')
        self.assertIn('(uint(gl_VertexID)*683u)>>12u', vertex)
        self.assertIn('uint(gl_VertexID)-index*6u', vertex)
        self.assertIn('kBatchQuads=128', layout.replace(' ', ''))
        self.assertIn('NVN_DRAW_PRIMITIVE_TRIANGLES,0,batch*6', native)
        corners = ((0,0), (1,0), (1,1), (0,0), (1,1), (0,1))
        for count in range(1, 129):
            for vertex_id in range(count*6):
                index = (vertex_id*683) >> 12
                corner = vertex_id-index*6
                self.assertEqual((index, corner), divmod(vertex_id, 6))
                self.assertLess(index, count)
                self.assertEqual((int(corner in (1,2,4)), int(corner in (2,4,5))), corners[vertex_id%6])
        # This lower-precision reciprocal really varies on the tested axis.
        self.assertTrue(any(((i*171)>>10) != i//6 for i in range(768)))

    def test_label_gpu_loads_keep_all_four_words_per_tap(self):
        loads = ''.join(f'/*0088*/ LDC R2, c[0x3][R1{offset}];\n'
                        for _ in range(4) for offset in ('', '+0x4', '+0x8', '+0xc'))
        good = loads + '/*0090*/ EXIT;'
        compiler.validate_gpu_assembly('labelFrag', good)
        with self.assertRaisesRegex(ValueError, 'all four'):
            compiler.validate_gpu_assembly('labelFrag', good.replace('+0x4', ''))
        with self.assertRaisesRegex(ValueError, 'saturation'):
            compiler.validate_gpu_assembly('labelFrag', good+'\n/*0098*/ FADD.FTZ.SAT R0, RZ, R4;')

    def test_label_ir_rejects_integer_saturation_and_lost_components(self):
        good = '\n'.join(f'MOV TEMP[{i}], CONST[2][ADDR[0].x]\n' +
                         '\n'.join(f'MOV TEMP[20].x, TEMP[{i}].{lane*4}' for lane in 'xyzw')
                         for i in range(4)) + '\n' + ('ISLT\nISGE\n' * 8)
        compiler.validate_label_ir(good)
        for bad, error in ((good+'MOV_SAT TEMP[0].xy, TEMP[1].xyyy', 'saturation'),
                           (good.replace('CONST[2][ADDR[0].x]', 'CONST[2][ADDR[0].x].xxxx'), 'complete'),
                           (good.replace('.yyyy', '.xxxx'), 'component'),
                           (good.replace('ISGE', 'USGE'), 'signed')):
            with self.subTest(error=error), self.assertRaisesRegex(ValueError, error):
                compiler.validate_label_ir(bad)

    def test_scan_fragment_rejects_local_memory_traffic(self):
        for instruction in ("LDL", "STL", "LDL.LU", "@P0 STL"):
            with self.subTest(instruction=instruction), self.assertRaisesRegex(ValueError, "local-memory"):
                compiler.validate_gpu_assembly("frag", f"/*0088*/ {instruction} R0, [R1];\n/*0090*/ EXIT;")

    def test_single_exit_shader_passes(self):
        compiler.validate_gpu_assembly("frag", "/*0088*/ TEXS R0, R1;\n/*0090*/ @P0 EXIT;")

    def test_return_stack_instructions_rejected(self):
        for instruction in ("PRET", "PRET.NOINC", "@P0 PRET.NOINC", "@!P1 PRET"):
            with self.subTest(instruction=instruction), self.assertRaisesRegex(ValueError, "PRET"):
                compiler.validate_gpu_assembly("frag", f"/*0088*/ {instruction} 0xe60;\n/*0090*/ EXIT;")

    def test_empty_or_incomplete_disassembly_rejected(self):
        for assembly in ("", ".headerflags SM53", "/*0088*/ MOV R0, R1;", "// EXIT"):
            with self.subTest(assembly=assembly), self.assertRaises(ValueError):
                compiler.validate_gpu_assembly("frag", assembly)

    def test_non_instruction_comment_is_ignored(self):
        compiler.validate_gpu_assembly("frag", "// PRET is unsupported\n/*0088*/ EXIT;")

if __name__ == "__main__":
    unittest.main()
