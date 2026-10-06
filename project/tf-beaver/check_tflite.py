import tensorflow as tf

models = [
    "models/best_triple_i256_m1_int8.tflite",
    "models/best_triple_i256_m2_int8.tflite",
    "models/best_triple_i256_m3_int8.tflite",
]


operators = []
for model in models:
    interpreter = tf.lite.Interpreter(model_path=model)
    interpreter.allocate_tensors()

    for op in interpreter._get_ops_details():
        operators.append(op["op_name"])

print("Operators used in the models:")
print(sorted(list(set(operators))))
