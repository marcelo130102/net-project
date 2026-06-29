import os
import sys

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CPP_DIR = os.path.join(BASE_DIR, "cpp_python_example")
sys.path.insert(0, CPP_DIR)

import numpy as np
import calculator

import torch
import torch.nn as nn
import numpy as np

class SimpleNN(nn.Module):
    def __init__(self):
        super(SimpleNN, self).__init__()
        self.fc1 = nn.Linear(4, 3)  # weight shape: [3, 4]

    def forward(self, x):
        return self.fc1(x)

model = SimpleNN()

original_weights_matrix = np.matrix(model.fc1.weight.data.cpu().numpy())
print("Original weights (np.matrix):\n", original_weights_matrix)

a = np.asarray(original_weights_matrix, dtype=np.float64)
n = calculator.matrix_add(a, a)

new_weights_matrix = np.matrix(n)

model.fc1.weight.data.copy_(torch.from_numpy(np.asarray(new_weights_matrix)))
print("Updated weights:\n", model.fc1.weight.data)




