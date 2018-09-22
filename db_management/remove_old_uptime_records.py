#!/usr/bin/python
import psycopg2  # pip install psycopg2
import logging
import sys

'''
This script will delete any uptime data more than 30 days old, except those records that are written either:
    1) Immediately after a reboot
    2) Immediately prior to a device rebooting or being taken offline
'''


con = None

logging.basicConfig(filename='/var/log/db_maintenance.log', level=logging.DEBUG)


try:
    con = psycopg2.connect("dbname='thingsboard'")
    cur = con.cursor()
    cur.execute("""
        WITH deletable AS (

        SELECT t.*
        FROM (SELECT *,
                     lead(long_v) over (partition by entity_id order by ts) as next_val,
                     lag(long_v) over (partition by entity_id order by ts) as prev_val
              FROM test -- ts_kv
              WHERE key = 'uptime'
                AND 
              ts < trunc(extract(epoch from (now() - interval '1 month')) * 1000)       -- Older than a month
             ) t
        WHERE long_v < next_val  -- delete where the next value is higher than this one... still growing!
                AND              -- but only where...
              long_v > prev_val  -- we are not the lowest;
        )
        
        DELETE FROM test K
        USING deletable
        WHERE K.entity_id = deletable.entity_id  AND  K.key = deletable.key  AND  K.ts = deletable.ts;
        """)

except psycopg2.DatabaseError as e:
    logging.error('Error removing useless uptime records: %s' % e)
    
finally:
    if con:
        con.close()