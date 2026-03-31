#!/bin/bash
npm install
cmake --build build -j4
npm run ui -- --port 4090
