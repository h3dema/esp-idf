import logging
import numpy as np
import onnx
import onnxruntime as ort


def validate_onnx_model(keras_model, onnx_path):

    logging.info("Validating ONNX model")
    onnx_model = onnx.load(onnx_path)
    onnx.checker.check_model(onnx_model)
    logging.info("ONNX schema validation passed")
    sample = np.random.randn(1, 224, 224, 3).astype(np.float32)
    keras_output = keras_model.predict(sample, verbose=0)
    session = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name
    onnx_output = session.run(None, {input_name: sample})[0]
    max_error = np.max(np.abs(keras_output - onnx_output))
    logging.info("Keras vs ONNX max error = %.8f", max_error)

    if max_error > 1e-3:
        raise RuntimeError(f"ONNX numerical validation failed (error={max_error})")

    logging.info("ONNX numerical validation passed")