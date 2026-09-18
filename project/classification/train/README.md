# ESP-DL MobileNetV1 CIFAR-10 Pipeline

The goal of this pipeline is to train a MobileNetV1 classifier on CIFAR-10, export it to ONNX, validate the conversion, perform ESP-DL post-training quantization, and generate deployable ESP32-S3 artifacts.

This project implements an end-to-end workflow:

1. Load and preprocess CIFAR-10.
2. Train a MobileNetV1 classifier using TensorFlow/Keras.
3. Export the trained model to ONNX.
4. Validate and evaluate the ONNX model.
5. Create a calibration dataset.
6. Quantize the model using ESP-DL.
7. Export deployable ESP-DL artifacts.
8. Compare floating-point and quantized model accuracy.


### Requirements

We recommend using [pyenv](https://github.com/pyenv/pyenv) to manage Python versions.
The following commands create a virtual environment and install the required dependencies:

```bash
pyenv install 3.11.11
pyenv virtualenv 3.11.11 espdl-mobilenet
pyenv activate espdl-mobilenet
pip install -r requirements.txt
```

### Running

The pipeline can be run using the following command:

```bash
python train_and_quantize.py \
    --epochs 30 \
    --batch-size 32 \
    --image-size 224 \
    --quantization int8 \
    --output-dir output
```

All generated artifacts are stored under the specified output directory.

### Dataset

The training pipeline expects the CIFAR-10 dataset and automatically splits it in training, validation, and test sets.
The pipeline also creates a calibration dataset for quantization (see `--calibration-samples`).


### Main Parameters

| Parameter | Description | Default |
|------------|-------------|---------|
| `--epochs` | Number of training epochs. | `30` |
| `--batch-size` | Batch size used during training. | `32` |
| `--learning-rate` | Initial learning rate for the optimizer. | `0.0001` |
| `--validation-split` | Fraction of the dataset reserved for validation. | `0.1` |
| `--dropout` | Dropout rate applied before the classification layer. | `0.2` |
| `--patience-es` | Early stopping patience. Training stops if validation loss does not improve for this number of epochs. | `5` |
| `--patience-lr` | Patience for learning-rate reduction. | `3` |
| `--lr-factor` | Factor applied when reducing the learning rate. | `0.5` |
| `--image-size` | Input image resolution used by MobileNetV1. | `224` |
| `--calibration-samples` | Number of images used to build the quantization calibration dataset. | `256` |
| `--alpha` | MobileNetV1 width multiplier. Smaller values reduce model size and computation cost. | `0.25` |
| `--seed` | Random seed used for reproducibility. | `42` |
| `--output-dir` | Directory where all generated artifacts are stored. | `output` |
| `--onnx-path` | Name of the exported ONNX model. Stored in `--output-dir`. | `model.onnx` |
| `--keras-path` | Name of the saved Keras model checkpoint. Stored in `--output-dir`. | `best_model.keras` |
| `--report-path` | Name of the quantization report. Stored in `--output-dir`. | `quantization_report.json` |
| `--calibration-dir` | Directory containing calibration images used by the ESP-DL quantizer. | `calibration/` |
| `--espdl-path` | Name of the generated ESP-DL model. Stored in `--output-dir/esp/`. | `model.espdl` |
| `--target-chip` | ESP target device. Supported values: `esp32`, `esp32s3`, `esp32c3`. | `esp32s3` |
| `--quantization` | Quantization format. Supported values: `int8`, `int16`. | `int8` |

All parameters are configurable through command-line arguments.

### Outputs

After a successful run, the following files are generated.

```text
output/
├── best_model.keras
├── model.onnx
├── calibration_data.npy
├── labels.txt
├── quantization_report.json
└── esp/
    ├── model.espdl
    ├── quantized.onnx
    └── quantized.json
```


#### Trained TensorFlow Model
Best checkpoint selected according to validation accuracy.
```text
output/best_model.keras
```


#### ONNX Model
Floating-point ONNX model exported from TensorFlow.
```text
output/model.onnx
```


#### Calibration Dataset
Calibration samples used for post-training quantization.
```text
output/calibration_data.npy
```

#### Calibration Directory
Image dataset used by the ESP-DL quantizer.
```text
calibration/
```

#### Quantized Models
Deployable quantized model for ESP targets and quantized ONNX representation.
```text
output/esp/model.espdl
output/esp/quantized.onnx
```

#### Class Labels

The labels file contains the CIFAR-10 class mapping:
- airplane, automobile, bird, cat, deer, dog, frog, horse, ship, truck

```text
output/labels.txt
```

#### Quantization Report
This report summarizes the impact of quantization on model accuracy.
```text
output/quantization_report.json
```

Example:
```text
{
    "keras_accuracy": 0.72,
    "onnx_accuracy": 0.72,
    "quantized_accuracy": 0.70,
    "accuracy_drop": 0.02,
    "calibration_samples": 256,
    "model": "mobilenetv1_alpha0.25"
}
```
