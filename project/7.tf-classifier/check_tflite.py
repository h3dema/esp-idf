import tensorflow as tf

models = [
    "/project/classification/train/model_int8.tflite",
]


operators = []
for model in models:
    interpreter = tf.lite.Interpreter(model_path=model)
    interpreter.allocate_tensors()

    for op in interpreter._get_ops_details():
        operators.append(op["op_name"])

print("Operators used in the models:")
print(sorted(list(set(operators))))
