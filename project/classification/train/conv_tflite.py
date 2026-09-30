from pathlib import Path
import numpy as np

import keras
import tensorflow as tf

from gen_espdl import parse_args, load_data

"""
# Basic environment

```bash
pyenv install 3.12
pyenv virtualenv 3.12 tf-mobilenet
pyenv activate tf-mobilenet
pip install -r requirements2.txt
```


"""

def representative_data_gen(train_ds, num_samples=100):
    count = 0
    for batch in train_ds:
        inputs = batch[0] if isinstance(batch, (tuple, list)) else batch
        for sample in inputs:
            sample = tf.cast(sample, tf.float32)
            sample = tf.expand_dims(sample, 0)
            yield [sample]
            count += 1
            if count >= num_samples:
                return


if __name__ == "__main__":
    print("Running conversion from keras to tflite")
    # import pdb; pdb.set_trace()
    args = parse_args()

    # Load the keras model
    model_path = Path(__file__).parent / "output/model.keras"

    print("Loading datasets")
    train_ds, val_ds, test_ds, training_images = load_data(args)

    print(f"Loading keras model from {model_path}")
    model = tf.keras.models.load_model(model_path)
    converter = tf.lite.TFLiteConverter.from_keras_model(model)

    # Enable optimizations
    converter.optimizations = [tf.lite.Optimize.DEFAULT]

    # Representative dataset is required for full INT8 quantization
    converter.representative_dataset = lambda: representative_data_gen(train_ds, 200)

    # Force INT8 operations
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]

    # INT8 input/output
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8

    tflite_model = converter.convert()

    with open("model_int8.tflite", "wb") as f:
        f.write(tflite_model)

    print("Saved model_int8.tflite")