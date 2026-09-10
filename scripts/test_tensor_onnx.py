#!/usr/bin/env python3
"""Compare the native tensor ONNX probe against ONNX ReferenceEvaluator/ORT.

Python and ONNX Runtime are test-only dependencies; the engine never links them.
Usage: python scripts/test_tensor_onnx.py --probe PATH --work PATH [--kokoro PATH]
"""
import argparse
import os
import re
import json
from pathlib import Path
import subprocess

import numpy as np
import onnx
from onnx import helper as h, numpy_helper as nh
from onnx.reference import ReferenceEvaluator
import onnxruntime as ort


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--kokoro', type=Path)
    parser.add_argument('--gpu', action='store_true')
    args = parser.parse_args()
    if args.gpu:
        os.environ['EVE_ONNX_GPU'] = '1'
    else:
        os.environ.pop('EVE_ONNX_GPU', None)
    args.work.mkdir(parents=True, exist_ok=True)
    probe = str(args.probe.resolve())
    results = []

    def check(name, nodes, values, outputs, reference='ort', tolerance=1e-6):
        initializers = [nh.from_array(np.asarray(v), k) for k, v in values.items()]
        graph = h.make_graph(nodes, name, [], [h.make_empty_tensor_value_info(o) for o in outputs], initializers)
        model = h.make_model(graph, ir_version=7, opset_imports=[h.make_opsetid('', 14), h.make_opsetid('com.microsoft', 1)])
        path = args.work / (name + '.onnx')
        onnx.save(model, path)
        if reference == 'ort':
            options = ort.SessionOptions()
            options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
            expected = ort.InferenceSession(model.SerializeToString(), options, providers=['CPUExecutionProvider']).run(outputs, {})
        else:
            expected = ReferenceEvaluator(model).run(outputs, {})
        for output, ref in zip(outputs, expected):
            raw = args.work / (name + '-' + output.replace('/', '_') + '.raw')
            result = subprocess.run([probe, str(path), output, str(raw)], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(name + ': ' + result.stdout + result.stderr)
            actual = np.fromfile(raw, dtype=ref.dtype).reshape(ref.shape)
            error = float(np.max(np.abs(actual.astype(np.float64) - ref.astype(np.float64)))) if ref.size else 0
            if np.issubdtype(ref.dtype, np.integer):
                np.testing.assert_array_equal(actual, ref)
            else:
                np.testing.assert_allclose(actual, ref, atol=tolerance, rtol=tolerance)
            dispatches=int(re.search(r'dispatches=(\d+)',result.stdout).group(1))
            if args.gpu and any(n.op_type in ['MatMulInteger','ConvInteger','DynamicQuantizeLSTM','MatMul','Add','Softmax','ReduceMean'] for n in nodes):
                assert dispatches>0, (name,result.stdout)
            results.append(dict(gpu_dispatches=dispatches, backend='gpu' if args.gpu else 'cpu', case=name, output=output, max_abs_error=error, tolerance=tolerance, reference=reference))
            print(name, output, 'PASS', error)

    for i, x in enumerate([[-1., 0, 1], [0., 0], [2., 3.], [-3., -2.], [-0.01, 0.03], [np.nan,1.,-1.], [np.nan,np.nan]]):
        check('dynamic' + str(i), [h.make_node('DynamicQuantizeLinear', ['x'], ['q','s','z'])],
              {'x': np.array(x, np.float32)}, ['q','s','z'], reference='ort')
    for sign in [False, True]:
        dtype = np.int8 if sign else np.uint8
        check('ties' + str(sign), [h.make_node('QuantizeLinear', ['x','s','z'], ['q'])],
              {'x':np.array([-1000,-1.25,-0.75,-0.25,0.25,0.75,1.25,1000],np.float32),
               's':np.array(0.5,np.float32),'z':np.array(0 if sign else 128,dtype)}, ['q'])
    check('per_axis', [h.make_node('DequantizeLinear',['q','s','z'],['y'],axis=1)],
          {'q':np.arange(24,dtype=np.uint8).reshape(2,3,4),'s':np.array([.1,.2,.3],np.float32),'z':np.array([2,5,8],np.uint8)}, ['y'])
    check('integer_exact', [h.make_node('MatMulInteger',['a','b'],['y'])],
          {'a':np.full((1,1025),255,np.uint8),'b':np.full((1025,1),127,np.int8)}, ['y'])
    check('signed_zero', [h.make_node('MatMulInteger',['a','b','az','bz'],['y'])],
          {'a':np.array([[-128,127,0]],np.int8),'b':np.array([[3,4],[5,6],[7,8]],np.uint8),'az':np.array(-3,np.int8),'bz':np.array(4,np.uint8)}, ['y'])
    check('conv_group', [h.make_node('ConvInteger',['x','w','xz','wz'],['y'],group=2,pads=[1,0,0,1],dilations=[1,2])],
          {'x':np.arange(2*4*4,dtype=np.uint8).reshape(1,2,4,4),'w':np.array(range(8),np.uint8).reshape(2,1,2,2),'xz':np.array(7,np.uint8),'wz':np.array(2,np.uint8)}, ['y'])
    check('conv1d', [h.make_node('ConvInteger',['x','w'],['y'],pads=[1,2],strides=[2])],
          {'x':np.arange(9,dtype=np.uint8).reshape(1,1,9),'w':np.array([1,2,3],np.uint8).reshape(1,1,3)}, ['y'])
    check('integer64_shape', [h.make_node('Gather',['x','i'],['y'])],
          {'x':np.array([2**60+1,-2**60-3],np.int64),'i':np.array([1,0],np.int64)}, ['y'])
    check('reshape_transpose', [h.make_node('Reshape',['x','s'],['r']), h.make_node('Transpose',['r'],['y'],perm=[1,0])],
          {'x':np.arange(12,dtype=np.float32),'s':np.array([3,-1],np.int64)}, ['y'])
    check('slice_reverse',[h.make_node('Slice',['x','s','e','a','st'],['y'])],
          {'x':np.arange(24,dtype=np.int64).reshape(2,3,4),'s':np.array([-1,-1],np.int64),'e':np.array([-100,-100],np.int64),'a':np.array([0,2],np.int64),'st':np.array([-1,-2],np.int64)},['y'])
    check('broadcast_empty',[h.make_node('Expand',['x','s'],['y'])],{'x':np.ones((1,3),np.float32),'s':np.array([0,3],np.int64)},['y'])
    check('concat',[h.make_node('Concat',['x','b'],['y'],axis=-1)],{'x':np.arange(6,dtype=np.int64).reshape(2,3),'b':np.array([[20],[30]],np.int64)},['y'])
    check('where',[h.make_node('Where',['c','a','b'],['y'])],{'c':np.array([[True],[False]]),'a':np.array([2**60+1,2**60+2],np.int64),'b':np.array(0,np.int64)},['y'])
    check('range_reverse',[h.make_node('Range',['s','e','d'],['y'])],{'s':np.array(2**60+9,np.int64),'e':np.array(2**60,np.int64),'d':np.array(-2,np.int64)},['y'],reference='reference')
    check('topk_ties',[h.make_node('TopK',['x','k'],['y','indices'],axis=0)],{'x':np.array([[2,4],[2,3],[1,5]],np.float32),'k':np.array([2],np.int64)},['y','indices'])
    check('scatter_nd',[h.make_node('ScatterND',['x','i','u'],['y'])],{'x':np.zeros((3,2),np.float32),'i':np.array([[-1],[0]],np.int64),'u':np.array([[4,5],[6,7]],np.float32)},['y'])
    check('reduce_product',[h.make_node('ReduceProd',['x'],['y'],axes=[0],keepdims=0)],{'x':np.array([[2,3],[4,5]],np.int64)},['y'])
    check('broadcast_float',[h.make_node('Add',['a','b'],['y'])],{'a':np.arange(24,dtype=np.float32).reshape(2,3,4),'b':np.array([[.1],[.2],[.3]],np.float32)},['y'])
    for name,a,b in [('batched',np.arange(24,dtype=np.float32).reshape(2,3,4)/20,np.arange(20,dtype=np.float32).reshape(4,5)/30),('vector',np.array([1.,2.,3.],np.float32),np.arange(6,dtype=np.float32).reshape(3,2)),('dot',np.array([1.,2.],np.float32),np.array([3.,4.],np.float32))]:
        check('matmul_'+name,[h.make_node('MatMul',['a','b'],['y'])],{'a':a,'b':b},['y'])
    check('pow_negative',[h.make_node('Pow',['x','p'],['y'])],{'x':np.array([-3.,-2.,-.1,0,2],np.float32),'p':np.array(2,np.float32)},['y'])
    check('reduce_mean',[h.make_node('ReduceMean',['x'],['y'],axes=[0,2],keepdims=0)],{'x':np.arange(24,dtype=np.float32).reshape(2,3,4)/7},['y'])
    check('softmax_axis',[h.make_node('Softmax',['x'],['y'],axis=1)],{'x':np.arange(24,dtype=np.float32).reshape(2,3,4)/7},['y'])
    for op in ['Reciprocal','Floor','Round','Atan','LeakyRelu','Sqrt','Exp','Log','Sin','Cos','Tanh','Sigmoid']:
        check('unary_'+op,[h.make_node(op,['x'],['y'])],{'x':np.array([.25,.5,1.,1.5,2.5,3.5],np.float32)},['y'],tolerance=2e-6)
    check('atan_infinity',[h.make_node('Atan',['x'],['y'])],{'x':np.array([-np.inf,np.inf],np.float32)},['y'],tolerance=2e-6)
    check('instance_norm',[h.make_node('InstanceNormalization',['x','s','b'],['y'],epsilon=1e-5)],{'x':np.arange(24,dtype=np.float32).reshape(2,3,4)/11,'s':np.array([1,2,3],np.float32),'b':np.array([.1,.2,.3],np.float32)},['y'],tolerance=3e-5)
    check('transpose_group',[h.make_node('ConvTranspose',['x','w','b'],['y'],group=2,strides=[2],pads=[1,1],output_padding=[1])],{'x':np.arange(12,dtype=np.float32).reshape(1,4,3)/13,'w':np.arange(24,dtype=np.float32).reshape(4,2,3)/7,'b':np.arange(4,dtype=np.float32)},['y'],tolerance=3e-6)
    for mode in ['nearest','linear']:
        check('resize_'+mode,[h.make_node('Resize',['x','','s'],['y'],mode=mode,coordinate_transformation_mode='asymmetric' if mode=='nearest' else 'half_pixel',nearest_mode='floor')],{'x':np.arange(6,dtype=np.float32).reshape(1,2,3),'s':np.array([1,1,2.5],np.float32)},['y'],tolerance=3e-6)
    for mode in ['reflect','edge','constant']:
        check('pad_'+mode,[h.make_node('Pad',['x','p'],['y'],mode=mode)],{'x':np.arange(6,dtype=np.float32).reshape(2,3),'p':np.array([0,2,0,1],np.int64)},['y'])
    for exclusive in [0,1]:
        check('cumsum_'+str(exclusive),[h.make_node('CumSum',['x','a'],['y'],exclusive=exclusive,reverse=1)],{'x':np.arange(24,dtype=np.float32).reshape(2,3,4),'a':np.array(1,np.int64)},['y'])
    check('clip',[h.make_node('Clip',['x','lo','hi'],['y'])],{'x':np.arange(6,dtype=np.float32),'lo':np.array(1.,np.float32),'hi':np.array(4.,np.float32)},['y'])
    branch=h.make_graph([h.make_node('Identity',['captured'],['branch_result'])],'branch',[],[h.make_tensor_value_info('branch_result',onnx.TensorProto.FLOAT,[2])])
    check('if_lexical_capture',[h.make_node('If',['condition'],['y'],then_branch=branch,else_branch=branch),h.make_node('Identity',['x'],['captured'])],{'condition':np.array(True),'x':np.array([2,3],np.float32)},['y'])
    body=h.make_graph([h.make_node('Identity',['condition_in'],['condition_out']),h.make_node('SequenceInsert',['state_in','iteration'],['state_out'])],'body',
          [h.make_tensor_value_info('iteration',onnx.TensorProto.INT64,[]),h.make_tensor_value_info('condition_in',onnx.TensorProto.BOOL,[]),h.make_tensor_sequence_value_info('state_in',onnx.TensorProto.INT64,[])],
          [h.make_tensor_value_info('condition_out',onnx.TensorProto.BOOL,[]),h.make_tensor_sequence_value_info('state_out',onnx.TensorProto.INT64,[])])
    check('loop_sequence',[h.make_node('SequenceEmpty',[],['empty'],dtype=onnx.TensorProto.INT64),h.make_node('Loop',['trip','condition','empty'],['sequence'],body=body),h.make_node('ConcatFromSequence',['sequence'],['y'],axis=0,new_axis=1)],{'trip':np.array(3,np.int64),'condition':np.array(True)},['y'])
    check('split_sequence',[h.make_node('SplitToSequence',['x','split'],['seq'],axis=1),h.make_node('SequenceAt',['seq','index'],['y'])],{'x':np.arange(12,dtype=np.int64).reshape(2,6),'split':np.array([2,1,3],np.int64),'index':np.array(-1,np.int64)},['y'])
    rng=np.random.default_rng(3)
    for direction in ['forward','reverse','bidirectional']:
        d=2 if direction=='bidirectional' else 1
        values={'x':rng.normal(size=(4,2,3)).astype(np.float32),
                'w':rng.integers(-60,60,(d,3,8),dtype=np.int8),'r':rng.integers(-60,60,(d,2,8),dtype=np.int8),
                'b':rng.normal(0,.1,(d,16)).astype(np.float32),
                'ws':np.full((d,8),.01,np.float32),'wz':np.zeros((d,8),np.int8),
                'rs':np.full((d,8),.01,np.float32),'rz':np.zeros((d,8),np.int8)}
        check('lstm_'+direction,[h.make_node('DynamicQuantizeLSTM',['x','w','r','b','','','','','ws','wz','rs','rz'],['y','yh','yc'],domain='com.microsoft',hidden_size=2,direction=direction)],values,['y','yh','yc'],reference='ort',tolerance=.004)
        values['lengths']=np.array([4,2],np.int32)
        values['h']=rng.normal(0,.1,(d,2,2)).astype(np.float32)
        values['c']=rng.normal(0,.1,(d,2,2)).astype(np.float32)
        check('lstm_state_'+direction,[h.make_node('DynamicQuantizeLSTM',['x','w','r','b','lengths','h','c','','ws','wz','rs','rz'],['y','yh','yc'],domain='com.microsoft',hidden_size=2,direction=direction)],values,['y','yh','yc'],reference='ort',tolerance=.004)
    if args.kokoro:
        model=onnx.load(args.kokoro)
        initializers={x.name:x for x in model.graph.initializer}
        for index, lstm in enumerate(n for n in model.graph.node if n.op_type=='DynamicQuantizeLSTM'):
            # Keep the original packed weights, scales, and node attributes.
            constants={k:nh.to_array(initializers[k]) for k in lstm.input if k in initializers}
            weight=constants[lstm.input[1]]
            constants[lstm.input[0]]=rng.normal(0,.1,(3,1,weight.shape[1])).astype(np.float32)
            for i in [4,5,6,7]:
                if i<len(lstm.input) and lstm.input[i] and lstm.input[i] not in constants:
                    lstm.input[i]=''
            check('kokoro_lstm_'+str(index),[lstm],constants,list(lstm.output),reference='ort',tolerance=1e-5)
    (args.work/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print('TENSOR_ONNX_PARITY_PASS',len(results),'outputs')


if __name__=='__main__':
    main()
