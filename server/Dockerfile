FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt server.py ./
RUN pip install --no-cache-dir -r requirements.txt

ENV DARPAN_HOST=0.0.0.0
ENV DARPAN_PORT=8765
ENV DARPAN_TLS=0

EXPOSE 8765
CMD ["python", "server.py"]
