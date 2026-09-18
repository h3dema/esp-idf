#!/usr/bin/env python3
import json
import argparse
import logging
import os
import random
import subprocess
from pathlib import Path

import numpy as np
import tensorflow as tf
import tf2onnx

from sklearn.model_selection import train_test_split
from tensorflow.keras import layers
from tensorflow.keras import models
from tensorflow.keras import callbacks
from tensorflow.keras import applications

from esp_ppq.api import espdl_quantize_onnx

from data import (
    load_data,
    build_augmentation,
    export_calibration_dataset,
    create_calibration_dataset,
    create_calibration_dataloader,
)
from validate_onnx_model import validate_onnx_model
from evaluate_onnx_model import evaluate_onnx_model

CIFAR10_CLASSES = [
    "airplane",
    "automobile",
    "bird",
    "cat",
    "deer",
    "dog",
    "frog",
    "horse",
    "ship",
    "truck",
]


def parse_args():
    parser = argparse.ArgumentParser(description="Train MobileNetV1 and export ESP-DL model")

    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--learning-rate", type=float, default=1e-4)
    parser.add_argument("--validation-split", type=float, default=0.1)
    parser.add_argument("--dropout", type=float, default=0.2)

    parser.add_argument("--patience-es", type=int, default=5)
    parser.add_argument("--patience-lr", type=int, default=3)
    parser.add_argument("--lr-factor", type=float, default=0.5)

    parser.add_argument("--image-size", type=int, default=224)
    parser.add_argument("--calibration-samples", type=int, default=256)

    parser.add_argument("--alpha", type=float, default=0.25)
    parser.add_argument("--seed", type=int, default=42)

    parser.add_argument("--onnx-path", default="model.onnx")
    parser.add_argument("--keras-path", default="best_model.keras")
    parser.add_argument("--espdl-path", default="model.espdl")
    parser.add_argument("--calibration-path", default="calibration_data.npy")

    parser.add_argument("--target-chip", default="esp32s3")
    parser.add_argument("--quantization", choices=["int8", "int16"], default="int8")

    parser.add_argument("--output-dir", type=Path, default=Path("output"))

    parser.add_argument("--report-path", default="quantization_report.json")
    parser.add_argument("--calibration-dir", default="calibration")

    return parser.parse_args()


def setup_logging():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s | %(levelname)s | %(message)s"
    )


def set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    tf.random.set_seed(seed)


def build_model(args):
    augmentation = build_augmentation()

    inputs = layers.Input(shape=(args.image_size, args.image_size, 3))

    x = augmentation(inputs)

    base_model = applications.MobileNet(
        input_shape=(args.image_size, args.image_size, 3),
        alpha=args.alpha,
        include_top=False,
        weights="imagenet"
    )

    base_model.trainable = False

    x = base_model(x, training=False)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(args.dropout)(x)

    outputs = layers.Dense(
        10,
        activation="softmax",
        name="predictions"
    )(x)

    model = models.Model(inputs, outputs)

    model.compile(
        optimizer=tf.keras.optimizers.Adam(args.learning_rate),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"]
    )

    return model, base_model


def train_model(model, train_ds, val_ds, args):
    cb = [
        callbacks.EarlyStopping(
            monitor="val_loss",
            patience=args.patience_es,
            restore_best_weights=True
        ),
        callbacks.ReduceLROnPlateau(
            monitor="val_loss",
            factor=args.lr_factor,
            patience=args.patience_lr,
            verbose=1
        ),
        callbacks.ModelCheckpoint(
            filepath=args.keras_path,
            monitor="val_accuracy",
            save_best_only=True,
            mode="max",
            verbose=1
        ),
    ]
    history = model.fit(train_ds, validation_data=val_ds, epochs=args.epochs, callbacks=cb)

    return history


def fine_tune(model, base_model, train_ds, val_ds, epochs=5):
    base_model.trainable = True
    for layer in base_model.layers[:-20]:
        layer.trainable = False

    model.compile(
        optimizer=tf.keras.optimizers.Adam(1e-5),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"]
    )
    model.fit(train_ds, validation_data=val_ds, epochs=epochs)

    return model


def evaluate_model(model, test_ds):
    loss, acc = model.evaluate(test_ds, verbose=1)
    logging.info("Test loss: %.4f", loss)
    logging.info("Test accuracy: %.4f", acc)

    return loss, acc


def export_onnx(model, output_path, image_size):
    # exportar com batch dinâmico
    spec = [tf.TensorSpec((None, image_size, image_size, 3), tf.float32, name="input")]
    tf2onnx.convert.from_keras(
        model,
        input_signature=spec,
        opset=13,
        output_path=output_path
    )
    logging.info("ONNX exported to %s", output_path)


def compare_accuracies(keras_acc, onnx_acc, quant_acc):
    logging.info("FP32 Keras     : %.4f", keras_acc)
    logging.info("FP32 ONNX      : %.4f", onnx_acc)
    logging.info("INT8 Quantized : %.4f", quant_acc)
    loss = (keras_acc - quant_acc) * 100
    logging.info("Quantization loss = %.2f%%", loss)
    if loss > 5.0:
        logging.warning("Accuracy degradation exceeds 5%%. Consider:")
        logging.warning("  * more calibration images")
        logging.warning("  * int16 quantization")
        logging.warning("  * fine-tuning")
        logging.warning("  * QAT")


def save_quantization_report(
    keras_acc,
    onnx_acc,
    quant_acc,
    calibration_samples,
    output_file,
):
    report = {
        "keras_accuracy": float(keras_acc),
        "onnx_accuracy": float(onnx_acc),
        "quantized_accuracy": float(quant_acc),
        "accuracy_drop": float(keras_acc - quant_acc),
        "calibration_samples": calibration_samples,
        "model": "mobilenetv1_alpha0.25",
    }
    with open(output_file, "w") as fp:
        json.dump(report, fp, indent=4)


def run_espdl_quantizer(args):
    logging.info("Running ESP-DL quantization")

    calibration_dataloader = create_calibration_dataloader(args.calibration_dir)
    quant_type = {
        "int8": "w8a8",
        "int16": "w16a16",
    }[args.quantization]

    espdl_quantize_onnx(
        onnx_import_file=str(args.onnx_path),
        espdl_export_file=str(args.espdl_path),
        calib_dataloader=calibration_dataloader,
        calib_steps=len(calibration_dataloader),
        input_shape=[1, args.image_size, args.image_size, 3],
        target=args.target_chip,
        quant_type=quant_type,
    )
    logging.info("Quantized model saved to %s", args.espdl_path)


def save_labels(path: Path):
    with open(path / "labels.txt", "w") as f:
        for label in CIFAR10_CLASSES:
            f.write(f"{label}\n")


def verify_outputs(args):
    files = [
        args.keras_path,
        args.onnx_path,
        args.calibration_dir,
    ]

    for file in files:
        if not Path(file).exists():
            raise FileNotFoundError(file)

    logging.info("Required artifacts created successfully")


def main():
    args = parse_args()

    # add output path to file names
    args.espdl_path = args.output_dir / "esp" /args.espdl_path
    args.keras_path = args.output_dir / args.keras_path
    args.onnx_path = args.output_dir / args.onnx_path
    args.calibration_path = args.output_dir / args.calibration_path
    args.report_path = args.output_dir / args.report_path

    # create output directory
    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.espdl_path.parent.mkdir(parents=True, exist_ok=True)

    setup_logging()
    set_seed(args.seed)

    logging.info("Loading datasets")
    train_ds, val_ds, test_ds, training_images = load_data(args)

    logging.info("Building model")
    model, base_model = build_model(args)

    # logging.info("Training classifier head")
    # train_model(
    #     model,
    #     train_ds,
    #     val_ds,
    #     args,
    # )

    # logging.info("Loading best checkpoint")
    # model = tf.keras.models.load_model(args.keras_path)

    # logging.info("Fine tuning")
    # model = fine_tune(
    #     model,
    #     base_model,
    #     train_ds,
    #     val_ds,
    # )

    # logging.info("Evaluating TensorFlow model")
    # keras_loss, keras_acc = evaluate_model(
    #     model,
    #     test_ds,
    # )

    # logging.info("Exporting ONNX")
    # export_onnx(
    #     model,
    #     args.onnx_path,
    #     args.image_size,
    # )

    # logging.info("Validating ONNX")
    # validate_onnx_model(
    #     keras_model=model,
    #     onnx_path=args.onnx_path,
    # )

    logging.info("Evaluating ONNX model")
    onnx_acc = evaluate_onnx_model(
        onnx_path=args.onnx_path,
        test_ds=test_ds,
    )

    logging.info("Preparing calibration dataset")
    export_calibration_dataset(
        x_train=training_images,
        num_samples=args.calibration_samples,
        output_dir=args.calibration_dir,
    )

    logging.info("Saving labels")
    save_labels(path=args.output_dir)

    verify_outputs(args)

    logging.info("Running ESP-DL quantizer")
    run_espdl_quantizer(args)
    if not os.path.exists(args.espdl_path):
        raise RuntimeError(f"ESP-DL model not generated: {args.espdl_path}")

    logging.info("ESP-DL model generated: %s", args.espdl_path)

    logging.info("Evaluating quantized model")
    quant_acc = evaluate_onnx_model(
        onnx_path=args.espdl_path.with_suffix(".onnx"),
        test_ds=test_ds,
    )
    keras_acc = 0.72  # TODO: remove me!!!!
    compare_accuracies(
        keras_acc=keras_acc,
        onnx_acc=onnx_acc,
        quant_acc=quant_acc,
    )

    save_quantization_report(
        keras_acc=keras_acc,
        onnx_acc=onnx_acc,
        quant_acc=quant_acc,
        calibration_samples=args.calibration_samples,
        output_file=args.report_path,
    )

    logging.info("Pipeline completed successfully")


if __name__ == "__main__":
    main()