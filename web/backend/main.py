from flask import Flask
from flask import request
from flask_cors import CORS
import redis

app = Flask(__name__)
CORS(app)

redisClient = redis.Redis()

@app.route('/redis/connect', methods = ['POST', 'GET'])
def connect():
    global redisClient
    if request.method == 'POST':
        uri = request.form.get('uri')
        try:
            redisClient = redis.Redis.from_url(uri)
            redisClient.ping()
        except Exception as e:
            return { 'success': False, 'error': str(e) }
        return { 'success': True }
    elif request.method == 'GET':
        try:
            redisClient.ping()
        except Exception as e:
            return { 'connected': False }
    return {}

@app.route('/redis/llen/<key>', methods = ['GET'])
def listLength():
    return { 'result': redisClient.llen(request.args['key']) }

@app.route('/queue/<queue>/memory', methods = ['GET'])
def getMemoryUsage(queue):
    pending = redisClient.memory_usage('cppq:' + queue + ':pending') or 0
    scheduled = redisClient.memory_usage('cppq:' + queue + ':scheduled') or 0
    active = redisClient.memory_usage('cppq:' + queue + ':active') or 0
    completed = redisClient.memory_usage('cppq:' + queue + ':completed') or 0
    failed = redisClient.memory_usage('cppq:' + queue + ':failed') or 0
    return { 'result': pending + scheduled + active + completed + failed }

@app.route('/queue/<queue>/stats', methods = ['GET'])
def queueStats(queue):
    pending = redisClient.llen('cppq:' + queue + ':pending')
    scheduled = redisClient.llen('cppq:' + queue + ':scheduled')
    active = redisClient.llen('cppq:' + queue + ':active')
    completed = redisClient.llen('cppq:' + queue + ':completed')
    failed = redisClient.llen('cppq:' + queue + ':failed')
    return { 'pending': pending, 'scheduled': scheduled, 'active': active, 'completed': completed, 'failed': failed }

@app.route('/queue', methods = ['GET'])
def queues():
    try:
        return { 'connected': True, 'queues': [x.decode('ascii') for x in list(redisClient.smembers('cppq:queues'))] }
    except Exception as e:
        return { 'connected': False }
