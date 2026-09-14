"""Regression checks for the editable-master boundary and failed publication."""
from io import BytesIO
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zipfile
import numpy as np
from PIL import Image
import export as exporter
from master_io import read_master, render, recipe, SLOTS

ROOT=Path(__file__).resolve().parent


def changed_archive(target, change):
    with zipfile.ZipFile(ROOT/'front.ora') as src, zipfile.ZipFile(target,'w') as dst:
        for entry in src.infolist():
            data=src.read(entry.filename)
            if entry.filename=='stack.xml':
                xml=ET.fromstring(data);change(xml);data=ET.tostring(xml)
            dst.writestr(entry,data)


class MasterContracts(unittest.TestCase):
    def test_saved_preview_matches_real_layer_stack(self):
        for pose in ['front','greeting']:
            with zipfile.ZipFile(ROOT/(pose+'.ora')) as archive:
                expected=Image.open(BytesIO(archive.read('mergedimage.png'))).convert('RGBA')
            actual=render(read_master(ROOT/(pose+'.ora')))
            np.testing.assert_array_equal(actual,expected)

    def test_complete_body_has_no_garment_dependent_holes(self):
        for pose in ['front','greeting']:
            body=render(read_master(ROOT/(pose+'.ora')),{'body':'base','hands':'base'})
            expected=Image.open(ROOT/(pose+'-body-reference.png')).convert('RGBA')
            np.testing.assert_array_equal(body,expected)

    def test_expression_changes_preserve_the_outer_face_contour(self):
        groups=read_master(ROOT/'front.ora')
        base=np.asarray(render(groups,recipe()))
        for expression in SLOTS['expression']:
            image=np.asarray(render(groups,recipe(expression=expression)))
            changed=np.any(image!=base,axis=2)
            changed[154:258,319:458]=False
            self.assertFalse(changed.any(),expression)

    def test_unsupported_blend_is_rejected_before_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            sentinel=root/'manifest.nut';sentinel.write_text('previous successful export')
            def change(xml):
                xml.find('stack')[1].set('composite-op','svg:multiply')
            changed_archive(root/'front.ora',change)
            with patch.object(exporter,'ROOT',root):
                with self.assertRaisesRegex(ValueError,'normal blend'):
                    exporter.export()
            self.assertEqual(sentinel.read_text(),'previous successful export')
            self.assertFalse((root/'exports').exists())

    def test_unknown_variant_fails_without_dropping_art(self):
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)/'bad.ora'
            changed_archive(target,lambda xml:xml.find('stack')[1].set('name','shirt.unknown'))
            with self.assertRaisesRegex(ValueError,'Unknown slot'):
                read_master(target)

    def test_normal_alpha_layer_edit_is_read_from_master(self):
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)/'edited.ora'
            def change(xml):
                for group in xml.find('stack'):
                    if group.get('name')=='accessory.star':
                        group[0].set('opacity','0.0')
            changed_archive(target,change)
            a=render(read_master(target),recipe())
            b=render(read_master(ROOT/'front.ora'),recipe(accessory=False))
            np.testing.assert_array_equal(a,b)


if __name__=='__main__':unittest.main()
