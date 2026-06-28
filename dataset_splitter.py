"""
csv data splitter
"""
import sys
import pandas as pd


def split_dataset(csv_path, parts):
	"""
	divide dataset
	"""

	df = pd.read_csv(csv_path)
	split_size = len(df) // parts
	splits = []

	# to divide
	for i in range(parts):
		start = i * split_size

		# add all at end
		if i == parts - 1:
			end = len(df)
		else:
			end = (i + 1) * split_size

		splits.append(df.iloc[start:end])

	# to create
	for i in range(parts):
		if i == 0:
			filename = "diabetes_master.csv"
		else:
			filename = f"diabetes_slave_{i}.csv"

		splits[i].to_csv(filename, index=False)
		print(f"Saved - {filename} - {len(splits[i])} rows")


if __name__ == "__main__":
	# args check
	if len(sys.argv) != 3:
		print("use: python dataset_splitter.py <file.csv> <number of parts>")
		sys.exit(1)

	file_csv = sys.argv[1]
	num_parts = int(sys.argv[2])

	split_dataset(file_csv, parts=num_parts)
