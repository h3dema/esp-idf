import onnx
import onnxruntime as ort
from onnx import TensorProto

import numpy as np
from pathlib import Path

model_path = Path(__file__).parent / "output/esp/quantized.onnx"

# Load ONNX model
model = onnx.load(model_path)
onnx.checker.check_model(model)

# Create ONNX Runtime session
session = ort.InferenceSession(
    model_path,
    providers=["CPUExecutionProvider"]
)

print("INPUT")

for x in session.get_inputs():
    print(x.name)
    print("shape:", x.shape)
    print("dtype:", x.type)
    print()


print("OUTPUT")

for x in session.get_outputs():
    print(x.name)
    print("shape:", x.shape)
    print("dtype:", x.type)
    print()


tensor_types = {}

for x in model.graph.input:
    tensor_types[x.name] = x.type.tensor_type.elem_type

for x in model.graph.output:
    tensor_types[x.name] = x.type.tensor_type.elem_type

for x in model.graph.value_info:
    tensor_types[x.name] = x.type.tensor_type.elem_type


def dtype_name(dtype):
    return TensorProto.DataType.Name(dtype)

print("OPERATORS")
for node in model.graph.node:

    input_types = [
        dtype_name(tensor_types[x])
        for x in node.input
        if x in tensor_types
    ]

    output_types = [
        dtype_name(tensor_types[x])
        for x in node.output
        if x in tensor_types
    ]

    types = input_types + output_types

    if "INT8" in types:
        precision = "INT8"
    elif "UINT8" in types:
        precision = "UINT8"
    elif "FLOAT16" in types:
        precision = "FP16"
    elif "FLOAT" in types:
        precision = "FP32"
    else:
        precision = "UNKNOWN"

    print(
        f"  {node.op_type}: {precision}"
        f"    input={input_types}"
        f"    output={output_types}"
    )

print("\nQUANTIZATION NODES")
for node in model.graph.node:
    if node.op_type in ["QuantizeLinear", "DequantizeLinear"]:
        print("\n", node.op_type)
        print("inputs :", list(node.input))
        print("outputs:", list(node.output))