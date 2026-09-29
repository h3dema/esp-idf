import tensorflow as tf

interpreter = tf.lite.Interpreter(
    model_path="output/model.tflite"
)

interpreter.allocate_tensors()

print("INPUT")
for x in interpreter.get_input_details():
    print(x["name"])
    print("shape:", x["shape"])
    print("dtype:", x["dtype"])
    print("quantization:", x["quantization"])

print("\nOUTPUT")

for x in interpreter.get_output_details():
    print(x["name"]
    print("shape:", x["shape"])
    print("dtype:", x["dtype"])
    print("quantization:", x["quantization"])

print("\nOPERATORS")

for op in interpreter._get_ops_details():

    print(op["op_name"])
