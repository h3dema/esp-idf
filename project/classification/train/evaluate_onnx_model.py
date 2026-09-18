import logging
import numpy as np
import onnxruntime as ort


def evaluate_onnx_model(onnx_path, test_ds):
    session = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name

    correct = 0
    total = 0
    for images, labels in test_ds:
        print(images.dtype, labels.dtype)
        predictions = session.run(None, {input_name: images.numpy()})[0]
        pred_classes = np.argmax(predictions, axis=1)
        labels = labels.numpy().flatten()
        correct += np.sum(pred_classes == labels)
        total += len(labels)
    accuracy = correct / total
    logging.info("ONNX accuracy: %.4f", accuracy)
    return accuracy
