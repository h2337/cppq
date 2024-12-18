#!/usr/bin/env bash

cd backend
python3 -m venv env
source env/bin/activate
pip3 install -r requirements.txt
flask --app main.py run &

cd ../frontend
yarn
yarn start

