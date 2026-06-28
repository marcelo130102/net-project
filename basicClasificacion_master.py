# Commented out IPython magic to ensure Python compatibility.
import os
import sys

# dir config
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CPP_DIR = os.path.join(BASE_DIR, "cpp_python_example")

sys.path.insert(0, CPP_DIR)

import modulo

# print(modulo.__file__)

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset, random_split

import torch.optim as optim
import torch.distributions as dist

import matplotlib.pyplot as plt
from sklearn.metrics import confusion_matrix, ConfusionMatrixDisplay, classification_report
import numpy as np

import pandas as pd
import math

import torch.distributions as dist

import copy


# args
if len(sys.argv) != 3:
	print("use: python basicClasificacion_master.py <num_slaves> <file_master.csv>")
	sys.exit(1)

num_slaves = int(sys.argv[1])
csv_path_arg = sys.argv[2]

# init master
print("master started...")
net = modulo.NetMaster(45000, num_slaves)


# conversion functions
def model_to_vector(model):
	all_tensors = [p.data.view(-1).cpu().numpy() for p in model.parameters()]
	return np.concatenate(all_tensors).astype(np.float32).reshape(1, -1)

def vector_to_model(flat_vector, model):
	flat_vector = flat_vector.flatten()
	current_idx = 0
	with torch.no_grad():
		for p in model.parameters():
			numel = p.numel()
			p.copy_(torch.from_numpy(flat_vector[current_idx : current_idx + numel]).view_as(p))
			current_idx += numel


X = None
y = None

class MulticlassClassifier(nn.Module):
	def __init__(self, input_dim: int, num_classes: int, hidden1: int = 128, hidden2: int = 64, hidden3: int = 32):
		super(MulticlassClassifier, self).__init__()
		self.fc1 = nn.Linear(input_dim, hidden1)
		self.fc2 = nn.Linear(hidden1, hidden2)
		self.fc3 = nn.Linear(hidden2, hidden3)
		self.class_logits = nn.Linear(hidden3, num_classes)	  # Predict class scores
		self.class_log_vars = nn.Linear(hidden3, num_classes)	 # Predict log-variance for each class

	def forward(self, x: torch.Tensor):
		x = F.relu(self.fc1(x))
		x = F.relu(self.fc2(x))
		x = F.relu(self.fc3(x))
		logits = self.class_logits(x)
		log_vars = self.class_log_vars(x)
		return logits, log_vars
		# return logits, logits


# Generate synthetic heteroscedastic multiclass data
torch.manual_seed(42)
num_samples = 1000
input_dim = 14
num_classes = 3
batch_size = 1

X = torch.randn(num_samples, input_dim)  # 1000 samples, 100 inputs

active_indices = torch.randint(0, num_classes, (num_samples,))
y = torch.nn.functional.one_hot(active_indices, num_classes=num_classes).float()

# y = torch.randint(0, num_classes, (num_samples * num_classes,)).view(num_samples, num_classes)

# Load dataset from CSV
# csv_path = "Dataset of Diabetes.csv"  # replace with your actual CSV file path   1000,13,3,50
csv_path = csv_path_arg

df = pd.read_csv(csv_path, header=None, skiprows=1)


# Assume first 4 columns are input features, last 3 are one-hot class labels
X_np = df.iloc[:, :input_dim].values.astype(np.float32)
# y_onehot_np = df.iloc[:, input_dim:].values.astype(np.float32)
y_onehot_np = df.iloc[:, -num_classes:].values.astype(np.float32)

y_np = np.argmax(y_onehot_np, axis=1).astype(np.int64)
y = torch.tensor(y_np)

X = torch.tensor(X_np)
# y = torch.tensor(y_onehot_np)
print("y ", y)
print("y ", y.size())
print("X ", X.size())
# Create dataset and split into training/testing
dataset = TensorDataset(X, y)

train_size = int(0.8 * len(dataset))
test_size = len(dataset) - train_size
train_dataset, test_dataset = random_split(dataset, [train_size, test_size])

train_loader = DataLoader(train_dataset, batch_size=batch_size, shuffle=True)
test_loader = DataLoader(test_dataset, batch_size=batch_size, shuffle=False)

# Model setup
model = MulticlassClassifier(input_dim=input_dim, num_classes=num_classes)

criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=0.001)

# Training loop
num_epochs = 10
train_tracker, test_tracker, accuracy_tracker = [], [], []

# Evaluation on test set
model.eval()
y_true = []
y_pred = []
log_vars_all = []

first_time = True

for epoch in range(num_epochs):
	model.train()
	epoch_loss = 0
	for batch_x, batch_y in train_loader:
		if first_time:
			print("\nmaster first training...")
			first_time = False
		else:
			# receive weights
			print("\nmaster waiting weights...")
			w_sum = None
			for i in range(num_slaves):
				w_slave = net.receive_matrix(i)
				if w_sum is None:
					w_sum = w_slave
				else:
					w_sum += w_slave
			# get avg
			w_avg = w_sum / num_slaves
			vector_to_model(w_avg, model)
			print(f"loaded weights-sample:\n{w_avg[0][:4]}")

		optimizer.zero_grad()
		logits, log_vars = model(batch_x)
		loss = criterion(logits, batch_y)
		loss.backward()
		optimizer.step()
		epoch_loss += loss.item()

		# send weights
		w_master = model_to_vector(model)
		# send weight
		for i in range(num_slaves):
			net.send_matrix(i, w_master)
		print(f"\nweights sended-sample:\n{w_master[0][:4]}")

	train_tracker.append(epoch_loss / len(train_loader))
	print(f"Epoch {epoch+1}/{num_epochs}, Loss: {train_tracker[-1]:.4f} | ",end="")


# with torch.no_grad():
	test_loss = 0
	total = 0
	num_correct = 0
	for batch_x, batch_y in test_loader:
		logits, log_vars = model(batch_x)
		loss = criterion(logits, batch_y)
		test_loss += loss.item()

		predictions = torch.argmax(logits, dim=1)
		total += batch_x.size(0)
		# num_correct += (predictions == torch.argmax(batch_y, dim=1)).sum().item()
		num_correct += (predictions == batch_y).sum().item()

		predictions = torch.argmax(logits, dim=1)
		# y_true.extend(torch.argmax(batch_y, dim=1))
		y_true.extend(batch_y.tolist())
		y_pred.extend(predictions.tolist())
		log_vars_all.append(log_vars)

	test_tracker.append(test_loss/len(test_loader))
	print(f"Test loss: {test_loss/len(test_loader)} | ", end='')
	accuracy_tracker.append(num_correct/total)
	print(f'Accuracy : {num_correct/total}')


## Plot training loss over epochs
#plt.figure(figsize=(8, 4))
#plt.plot(train_tracker, marker='o')
#plt.title("Training Loss Over Epochs")
#plt.xlabel("Epoch")
#plt.ylabel("Loss")
#plt.grid(True)
#plt.tight_layout()
#plt.show()
#
#import matplotlib.pyplot as plt
## %matplotlib inline
#plt.plot(train_tracker, label='Training loss')
#plt.plot(test_tracker, label='Test loss')
#plt.plot(accuracy_tracker, label='Test accuracy')
#plt.legend()
#
#
## Display confusion matrix
#cm = confusion_matrix(y_true, y_pred)
#disp = ConfusionMatrixDisplay(confusion_matrix=cm, display_labels=list(range(num_classes)))
#disp.plot(cmap=plt.cm.Blues)
#plt.title("Confusion Matrix")
#plt.tight_layout()
#plt.show()
#
## Print classification metrics
#print("\nClassification Report:")
#print(classification_report(y_true, y_pred, digits=3))
