import json
from pathlib import Path
import logging
import numpy as np

from sklearn.model_selection import train_test_split

import torch
from torch.utils.data import Dataset, DataLoader

import tensorflow as tf
from tensorflow.keras import layers
from tensorflow.keras import applications


def build_augmentation():
    return tf.keras.Sequential([
        layers.RandomFlip("horizontal"),
        layers.RandomRotation(0.1),
        layers.RandomZoom(0.1),
    ])


def preprocess_image(image, label, size):
    image = tf.image.resize(image, [size, size], method="bilinear")
    image = tf.cast(image, tf.float32)
    image = applications.mobilenet.preprocess_input(image)
    return image, label


def create_dataset(images, labels, batch_size, image_size, training=False):
    ds = tf.data.Dataset.from_tensor_slices((images, labels))

    if training:
        ds = ds.shuffle(len(images))

    ds = ds.map(
        lambda x, y: preprocess_image(x, y, image_size),
        num_parallel_calls=tf.data.AUTOTUNE
    )

    ds = ds.batch(batch_size)
    ds = ds.prefetch(tf.data.AUTOTUNE)

    return ds


def load_data(args):
    (x_train, y_train), (x_test, y_test) = tf.keras.datasets.cifar10.load_data()

    x_train, x_val, y_train, y_val = train_test_split(
        x_train,
        y_train,
        test_size=args.validation_split,
        random_state=args.seed,
        stratify=y_train
    )

    logging.info("Train samples: %d", len(x_train))
    logging.info("Validation samples: %d", len(x_val))
    logging.info("Test samples: %d", len(x_test))

    train_ds = create_dataset(
        x_train,
        y_train,
        args.batch_size,
        args.image_size,
        training=True
    )

    val_ds = create_dataset(
        x_val,
        y_val,
        args.batch_size,
        args.image_size
    )

    test_ds = create_dataset(
        x_test,
        y_test,
        args.batch_size,
        args.image_size
    )

    return train_ds, val_ds, test_ds, x_train



def create_calibration_dataset(images, args):
    count = min(args.calibration_samples, len(images))

    indices = np.random.choice(
        len(images),
        size=count,
        replace=False
    )
    calib = images[indices]

    resized = []
    for image in calib:
        img = tf.image.resize(
            image,
            [args.image_size, args.image_size],
            method="bilinear"
        )
        img = applications.mobilenet.preprocess_input(img)
        resized.append(img.numpy())

    resized = np.asarray(resized, dtype=np.float32)
    np.save(args.calibration_path, resized)
    logging.info(
        "Calibration dataset saved: %s (%d samples)",
        args.calibration_path,
        len(resized)
    )

    return resized




def export_calibration_dataset(x_train, num_samples, imgsz=224, output_dir="calibration"):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    selected_indices = np.random.choice(len(x_train), num_samples, replace=False)
    for i, idx in enumerate(selected_indices):
        image = x_train[idx]
        image = tf.image.resize(image, (imgsz, imgsz), method="bilinear")
        image = tf.cast(image, tf.float32)
        image = applications.mobilenet.preprocess_input(image)
        np.save(output_dir / f"{i:05d}.npy", image.numpy())

    metadata = {
        "num_samples": num_samples,
        "shape": [224, 224, 3],
        "dtype": "float32",
    }


    with open(output_dir / "metadata.json", "w") as fp:
        json.dump(metadata, fp, indent=2)

    logging.info("Calibration dataset exported to %s", output_dir)





class NpyCalibrationDataset(Dataset):
    def __init__(self, root_dir):
        self.files = sorted(Path(root_dir).glob("*.npy"))

        if not self.files:
            raise RuntimeError(f"No .npy files found in {root_dir}")

    def __len__(self):
        return len(self.files)

    def __getitem__(self, idx):
        x = np.load(self.files[idx]).astype(np.float32)

        # Return only the input tensor for calibration
        return torch.from_numpy(x)


def create_calibration_dataloader(calibration_dir, batch_size=32):
    dataset = NpyCalibrationDataset(calibration_dir)

    return DataLoader(
        dataset,
        batch_size=batch_size,
        shuffle=False,
        drop_last=False,
        num_workers=0,
    )