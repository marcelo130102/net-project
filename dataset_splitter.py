import pandas as pd


# test with 3
def split_dataset(csv_path, parts=3):
	"""to split dataset into parts"""

	df = pd.read_csv(csv_path)
	df_shuffled = df.sample(frac=1, random_state=42).reset_index(drop=True)

	split_size = len(df_shuffled) // parts

	splits = []

	for i in range(parts):
		start = i * split_size

		if i == parts - 1:
			end = len(df_shuffled)
		else:
			end = (i + 1) * split_size

		splits.append(df_shuffled.iloc[start:end])

	# TODO: change the logic
	splits[0].to_csv("diabetes_master.csv", index=False)
	splits[1].to_csv("diabetes_slave1.csv", index=False)
	splits[2].to_csv("diabetes_slave2.csv", index=False)


if __name__ == "__main__":
	split_dataset("Dataset of Diabetes.csv", parts=3)
