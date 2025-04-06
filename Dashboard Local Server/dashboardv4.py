import panel as pn
import pandas as pd
import numpy as np
import joblib
import os
import matplotlib.pyplot as plt
import time  # For delay in updates

# Enable Panel extensions
pn.extension()

# Load the trained model
model_path = "random_forest_model.pkl"

if os.path.exists(model_path):
    model = joblib.load(model_path)
    print("Model loaded successfully.")
else:
    raise FileNotFoundError(f"Model not found at {model_path}. Please check the file path.")

crop_labels = model.classes_  # Get crop class names from trained model

# Define CSV file paths
csv_files = ["mango.csv", "maize.csv"]
current_csv_index = 0  # Start with maize.csv
periodic_callback = None  # To track the update loop

# Load CSV file function
def load_csv(file_path):
    if os.path.exists(file_path):
        try:
            df = pd.read_csv(file_path, dtype=float)  # Force numeric types
            if df.empty:
                raise ValueError("The CSV file is empty.")
            print(f"CSV data loaded successfully from {file_path}")
            return df
        except Exception as e:
            raise ValueError(f"Error loading CSV file {file_path}: {e}")
    else:
        raise FileNotFoundError(f"CSV file not found at {file_path}. Please check the file path.")

# Load initial CSV file
df = load_csv(csv_files[current_csv_index])

# Styled Header (Centered, Larger Font)
header = pn.pane.Markdown(
    """
    <div style="text-align: center; font-size: 72px; font-weight: bold; color: #2E8B57;">
        🌱 Welcome to Agrivisor
    </div>
    """,
    width=1450  
)

# Define max bounds for each parameter
max_bounds = {
    "Nitrogen": 100.0,
    "Phosphorus": 60.0,
    "Potassium": 40.0,
    "Temperature": 40.0,
    "Humidity": 75.0,
    "pH_Value": 12.0,
    "Rainfall": 110.0
}

# Create gauges for visualization (initialized with first row)
initial_values = df.iloc[0].to_dict()  # Start with first row
gauges = {
    name: pn.indicators.Dial(
        name=name, 
        value=round(float(value), 1),  # Convert to float and round
        bounds=(0, max_bounds.get(name, 200)),  # Set custom max bound
        format="{value:.1f}",  # Force display of 1 decimal place
        height=160, 
        width=160
    )
    for name, value in initial_values.items()
}

# Centering Gauges using a GridBox
gauge_layout = pn.GridBox(*gauges.values(), ncols=7, align="center")

# Output Text Box (Initially Empty)
output_text = pn.pane.Markdown(
    """
    <div style="text-align: left; font-size: 18px;">
        <b>🌾 The suggested crop for your lands are: _[Waiting for analysis...]_</b>
    </div>
    """,
    width=800  
)

# Bar Chart Placeholder
bar_chart_pane = pn.pane.Matplotlib()

# Function to Run Model and Update Output Text, Gauges, and Bar Chart
def run_prediction(index=0):
    global periodic_callback
    if index >= len(df):
        return  # Stop if all rows are processed

    # Get current row data
    input_values = df.iloc[index].to_dict()

    # Update gauges dynamically
    for name, value in input_values.items():
        try:
            gauges[name].value = round(float(value), 1)  # Ensure numeric conversion
        except ValueError:
            gauges[name].value = 0.0  # Default value for non-numeric inputs

    # Prepare input for the model
    input_array = np.asarray(list(input_values.values())).reshape(1, -1)
    probabilities = model.predict_proba(input_array)[0]  # Get probabilities for all crops

    # Sort crops by highest probability
    sorted_indices = np.argsort(probabilities)[::-1]
    sorted_probs = probabilities[sorted_indices]  # Sorted confidence scores
    sorted_labels = crop_labels[sorted_indices]  # Sorted crop names

        # Get top 3 crops
    top_3_crops = sorted_labels[:3]
    top_3_confidences = sorted_probs[:3]

    # Convert confidence scores:
    # - Crop #1 remains at 100%
    # - Crop #2 and #3 are scaled to 150%
    top_3_confidences = [
        top_3_confidences[0] * 100,   # Scale first crop normally
        top_3_confidences[1] * 100 * 5,   # Scale second crop to 150%
        top_3_confidences[2] * 100 * 5    # Scale third crop to 150%
    ]


    # Predicted crop (highest confidence)
    predicted_crop = top_3_crops[0]  

    # Update Prediction Output
    output_text.object = f"""
    <div style="text-align: left; font-size: 18px;">
        <b>🌾 The suggested crop for your lands are: {predicted_crop}</b>
    </div>
    """

    # Create Bar Chart with Custom Size
    fig, ax = plt.subplots(figsize=(5, 2.5))  # (Width, Height of entire chart)
    colors = ['#FF5733', '#33FF57', '#3380FF']  # Different colors for each crop

    ax.barh(top_3_crops[::-1], top_3_confidences[::-1], color=colors, height=0.6)  # Flip order for display
    ax.set_title("Top 3 Crop Suggestions", fontsize=8, fontweight='bold')
    ax.set_xlim(0, 100)
    ax.set_xlabel("Confidence (%)", fontsize=8)

    # Move the bar chart slightly lower
    plt.subplots_adjust(left=0.15, right=0.95, top=0.85, bottom=0.15)  

    # Increase font size of axis labels
    ax.tick_params(axis='both', labelsize=8)

    # Update Bar Chart Panel
    bar_chart_pane.object = fig

    # Schedule the next row update after a delay
    periodic_callback = pn.state.add_periodic_callback(lambda: run_prediction(index + 1), period=2000, count=1)

# Start looping through rows in the CSV
run_prediction(0)

# Button to switch CSV files
def switch_csv(event):
    global current_csv_index, df, periodic_callback

    # Stop ongoing periodic updates
    if periodic_callback:
        periodic_callback.stop()

    # Switch CSV file
    current_csv_index = (current_csv_index + 1) % len(csv_files)  # Switch between files
    df = load_csv(csv_files[current_csv_index])  # Load new file

    # Restart predictions with new data
    run_prediction(0)

# Create button widget
switch_button = pn.widgets.Button(name="Switch CSV File", button_type="primary")
switch_button.on_click(switch_csv)  # Bind button click to function

# Embed a website (YouTube example) using HTML iframe
embedded_website = pn.pane.HTML(
    """
    <iframe src="https://www.google.com/" width="600" height="300" style="border: none;"></iframe>
    """,
    width=600,
    height=300
)

# Layout to place the bar chart and website side by side
bar_chart_with_website = pn.Row(
    bar_chart_pane,  # Bar chart
    pn.Spacer(width=20),  # Add some spacing
    embedded_website  # Website iframe
)

# Layout the dashboard
dashboard = pn.Column(
    header,
    switch_button,  # Add the switch button to the dashboard
    "## 🌾 Current Soil Conditions:",
    pn.layout.HSpacer(),
    gauge_layout,
    pn.layout.HSpacer(),
    pn.Spacer(height=20),  # Add some spacing before the output text
    output_text,
    bar_chart_with_website  # Add Bar Chart and Website Box Side by Side
)

dashboard.servable()
