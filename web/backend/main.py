from flask import Flask
from flask import request
from flask_cors import CORS
import redis

app = Flask(__name__)
CORS(app)

redisClient = redis.Redis()

def decode_redis(src):
    if isinstance(src, list):
        rv = list()
        for key in src:
            rv.append(decode_redis(key))
        return rv
    elif isinstance(src, dict):
        rv = dict()
        for key in src:
            rv[key.decode()] = decode_redis(src[key])
        return rv
    elif isinstance(src, bytes):
        return src.decode()
    else:
        raise Exception("type not handled: " + type(src))

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
def listLength(key):
    return { 'result': redisClient.llen(key) }

@app.route('/redis/llrangeAll/<key>', methods = ['GET'])
def llrangeAll(key):
    return { 'result': redisClient.lrange(key, 0, -1) }


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

@app.route('/queue/<queue>/<state>/tasks', methods = ['GET'])
def queueTasks(queue, state):
    taskUuids = [x.decode('ascii') for x in redisClient.lrange('cppq:' + queue + ':' + state, 0, -1)]
    tasks = []
    for uuid in taskUuids:
        tasks.append(decode_redis(redisClient.hgetall('cppq:' + queue + ':task:' + uuid)))
    return { 'result': tasks }

@app.route('/queue', methods = ['GET'])
def queues():
    try:
        return { 'connected': True, 'queues': [x.decode('ascii') for x in list(redisClient.smembers('cppq:queues'))] }
    except Exception as e:
        return { 'connected': False }
