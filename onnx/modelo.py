from ultralytics import YOLO
modelo = YOLO("yolov8n.pt")
modelo.export(format="onnx")
