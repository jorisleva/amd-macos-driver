"""GPU-free checks of the published Metal graphics admission/provenance gate."""
import importlib.util
import json
import shutil
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('metal_graphics', ROOT / 'tools/compile-metal-graphics.py')
graphics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(graphics)


class MetalGraphicsContract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.corpus = Path(self.tmp.name) / 'corpus'
        shutil.copytree(ROOT / 'tests/shaders/apple/graphics', self.corpus)

    def tearDown(self):
        self.tmp.cleanup()

    def metadata(self, name, change):
        path = self.corpus / (name + '.reflection.json')
        meta = json.loads(path.read_text())
        change(meta)
        path.write_text(json.dumps(meta))

    def test_published_hashes_and_contract(self):
        contract = graphics.check_corpus(self.corpus)
        self.assertTrue(contract['texture.frag']['requires_shader_int8'])
        self.assertEqual(contract['texture.frag']['descriptors'], [(0, 32), (0, 160)])

    def test_corrupted_source_hash(self):
        path = self.corpus / 'texture.frag.metal'
        path.write_bytes(path.read_bytes() + b'\n')
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            graphics.check_corpus(self.corpus)

    def test_wrong_stage(self):
        self.metadata('texture.frag', lambda m: m.update(stage='Vertex'))
        with self.assertRaisesRegex(ValueError, 'stage/schema/entry'):
            graphics.check_corpus(self.corpus, False)

    def test_wrong_descriptor_reflection(self):
        self.metadata('texture.frag', lambda m: m['bindings'][0]['descriptor'].update(binding=33))
        with self.assertRaisesRegex(ValueError, 'descriptor ABI'):
            graphics.check_corpus(self.corpus, False)

    def test_wrong_draw_layout(self):
        self.metadata('triangle.vert', lambda m: m['bindings'][0]['type_layout']['Struct'][1].update(offset=20))
        with self.assertRaisesRegex(ValueError, 'layout/access'):
            graphics.check_corpus(self.corpus, False)

    def test_emitted_descriptor_mismatch(self):
        path = self.corpus / 'texture.frag.spv'
        data = path.read_bytes()
        words = list(struct.unpack('<' + 'I' * (len(data) // 4), data))
        i = 5
        while i < len(words):
            if words[i] & 0xffff == 71 and words[i] >> 16 == 4 and words[i + 2] == 33:
                words[i + 3] += 1
                break
            i += words[i] >> 16
        path.write_bytes(struct.pack('<' + 'I' * len(words), *words))
        with self.assertRaisesRegex(ValueError, 'reflection disagrees'):
            graphics.check_corpus(self.corpus, False)

    def test_bindless_capability_refused(self):
        path = self.corpus / 'texture.frag.spv'
        path.write_bytes(path.read_bytes() + struct.pack('<II', (2 << 16) | 17, 5302))
        with self.assertRaisesRegex(ValueError, 'unsupported capability'):
            graphics.check_corpus(self.corpus, False)

    def test_truncated_spirv(self):
        path = self.corpus / 'texture.frag.spv'
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaisesRegex(ValueError, 'SPIR-V length'):
            graphics.check_corpus(self.corpus, False)


if __name__ == '__main__':
    unittest.main()
