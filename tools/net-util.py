#!/usr/bin/env python3

import argparse
import datetime
import ipaddress
import logging
import pathlib
import requests
import sys
import time

from types import MethodType
from typing import Callable

import urwid
import urwid.curses_display

from prometheus_client.parser import text_string_to_metric_families
from urwid.canvas import apply_text_layout
from urwid.widget import WidgetError

logging.TRACE = 5
logging.addLevelName(5, 'TRACE')
assert logging.TRACE < logging.DEBUG, 'Logging TRACE level expected to be lower than DEBUG'
assert logging.getLevelName('TRACE') < logging.getLevelName('DEBUG'), 'Logging TRACE level expected to be lower than DEBUG'

PROMETHEUS_URL = '/v1/prometheus/metrics'

logger = logging.getLogger(__name__)

def humanReadableBytesPerSecond(bytes: int, telco:bool = False):
    power = 10**3 if telco else 2**10
    n = 0
    labels = {0: '', 1: 'K', 2: 'M', 3: 'G', 4: 'T'} if telco else {0: '', 1: 'Ki', 2: 'Mi', 3: 'Gi', 4: 'Ti'}
    while bytes > power:
        bytes /= power
        n += 1
    return f'{"-" if bytes == 0.0 else "~0" if bytes < 0.01 else format(bytes, ".2f")} {labels[n]}B/s'


class TextSimpleFocusListWalker(urwid.SimpleFocusListWalker):
    def __contains__(self, text):
        for element in self:
            if type(element) is urwid.AttrMap:
                val = element.original_widget.text
            else:
                val = element.text
            if text == val:
                return True
        return False
    def index(self, text):
        '''Emulation of list index() method unfortunately much slower than the real thing but our lists are short'''
        for i, e in enumerate(self):
            if type(e) is urwid.AttrMap:
                val = e.original_widget.text
            else:
                val = e.text
            if val == text:
                return i


class ColumnedListPile(urwid.Pile):
    def __init__(self, widget_list, focus_item=None):
        super().__init__(widget_list, focus_item)
        self.allColumns = []
    def setAllColumns(self, columns):
        self.allColumns = columns
    def render(self, size, focus=False):
        super().render(size, focus)
        maxrows = 0
        for pile in self.allColumns:
            _, rows = pile.contents[0][0].pack()
            if rows > maxrows:
                maxrows = rows
        for pile in self.allColumns:
            _, rows = pile.contents[0][0].pack()
            if rows < maxrows:
                text = pile.contents[0][0].text
                pile.contents[0][0].text = (maxrows - rows)*'\n'+text


def readMetrics(host: str, port: str):
    response = requests.get(f'http://{host}:{port}{PROMETHEUS_URL}', timeout=10)
    if response.status_code != 200:
        logger.fatal(f'Prometheus metrics URL returned {response.status_code}: {response.url}')
        raise urwid.ExitMainLoop()
    return response

class netUtil:
    def __init__(self):
        self.prometheusMetrics = {
            ('nodeos_info', 'server_version'): 'Nodeos Version ID:',
            ('nodeos_info', 'chain_id'): 'Chain ID:',
            ('nodeos_info', 'server_version_string'): 'Nodeos Version:',
            ('nodeos_info', 'server_full_version_string'): 'Nodeos Full Version:',
            ('nodeos_info', 'earliest_available_block_num'): 'Earliest Available Block:',
            'nodeos_head_block_num': 'Head Block Num:',
            'nodeos_last_irreversible': 'LIB:',
            'nodeos_p2p_clients': 'Inbound P2P Connections:',
            'nodeos_p2p_peers': 'Outbound P2P Connections:',
            'nodeos_blocks_incoming_total': 'Total Incoming Blocks:',
            'nodeos_trxs_incoming_total': 'Total Incoming Trxs:',
            'nodeos_blocks_produced_total': 'Blocks Produced:',
            'nodeos_trxs_produced_total': 'Trxs Produced:',
            'nodeos_scheduled_trxs_total': 'Scheduled Trxs:',
            'nodeos_unapplied_transactions_total': 'Unapplied Trxs:',
            'nodeos_p2p_dropped_trxs_total': 'Dropped Trxs:',
            'nodeos_p2p_failed_connections_total': 'Failed P2P Connections:',
            'nodeos_http_requests_total': 'HTTP Requests:',
        }
        self.ignoredPrometheusMetrics = [
            'nodeos_exposer_scrapes_total',
            'nodeos_exposer_transferred_bytes_total',
            'nodeos_subjective_bill_account_size_total',
            'nodeos_net_usage_us_total',
            'nodeos_cpu_usage_us_total',
        ]
        self.leftFieldLabels = [
            'Host:',
            'Head Block Num:',
            'Inbound P2P Connections:',
            'Failed P2P Connections:',
            'Total Incoming Blocks:',
            'Blocks Produced:',
            'Scheduled Trxs:',
            'Unapplied Trxs:',
            'HTTP Requests:',
        ]
        self.rightFieldLabels = [
            'Nodeos Version:',
            'LIB:',
            'Outbound P2P Connections:',
            'Total Incoming Trxs:',
            'Trxs Produced:',
            'Blacklisted Trxs:',
            'Dropped Trxs:',
        ]
        self.peerMetricConversions = {
            'hostname': lambda x: x[1:].replace('__', ':').replace('_', '.'),
            'port': lambda x: str(int(x)),
            'accepting_blocks': lambda x: 'True' if x else 'False',
            'latency': lambda x: format(int(x)/1000000, '.2f') + ' ms',
            'last_received_block': lambda x: str(int(x)),
            'first_available_block': lambda x: str(int(x)),
            'last_available_block': lambda x: str(int(x)),
            'unique_first_block_count': lambda x: str(int(x)),
            'last_bytes_received': lambda x: str(datetime.timedelta(microseconds=(time.time_ns() - int(x))/1000)),
            'last_bytes_sent': lambda x: str(datetime.timedelta(microseconds=(time.time_ns() - int(x))/1000)),
        }
        self.infoFieldLabels = [
            'Nodeos Version ID:',
            'Chain ID:',
            'Nodeos Full Version:',
            'Earliest Available Block:',
        ]
        self.peerColumns = [
            ('Connection ID', 'connectionIDLW'),
            ('\n\nIP Address', 'ipAddressLW'),
            ('\n\nPort', 'portLW'),
            ('\n\nHostname', 'hostnameLW'),
            ('\n\nLatency', 'latencyLW'),
            ('\nSend\nRate', 'sendBandwidthLW'),
            ('Last\nSent\nTime', 'lastBytesSentLW'),
            ('\nRcv\nRate', 'receiveBandwidthLW'),
            ('Last\nRcv\nTime', 'lastBytesReceivedLW'),
            ('Last\nRcvd\nBlock', 'lastReceivedBlockLW'),
            ('Blk\nSync\nRate', 'blockSyncBandwidthLW'),
            ('Unique\nFirst\nBlks', 'uniqueFirstBlockCountLW'),
            ('First\nAvail\nBlk', 'firstAvailableBlockLW'),
            ('Last\nAvail\nBlk', 'lastAvailableBlockLW'),
            ('\nAcpt\nBlks', 'acceptingBlocksLW')
        ]
        def labelToAttrName(label: str, fieldType='Text'):
            return label[:1].lower() + label[1:-1].replace(' ', '') + fieldType
        self.fields = {k:v for k, v in zip(self.leftFieldLabels, [labelToAttrName(e) for e in self.leftFieldLabels])}
        self.fields.update({self.rightFieldLabels[0]: labelToAttrName(self.rightFieldLabels[0], 'Button')})
        self.fields.update({k:v for k, v in zip(self.rightFieldLabels[1:], [labelToAttrName(e) for e in self.rightFieldLabels[1:]])})
        self.fields.update({k:v for k, v in zip(self.infoFieldLabels, [labelToAttrName(e) for e in self.infoFieldLabels])})
        
        parser = argparse.ArgumentParser(description='Terminal UI for monitoring nodeos P2P connections',
                                         formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument('--host', help='hostname or IP address to connect to', default='127.0.0.1')
        parser.add_argument('-p', '--port', help='port number to connect to', default='8888')
        parser.add_argument('--refresh-interval', help='refresh interval in seconds (max 25.5)', default='25.5')
        parser.add_argument('--log-level', choices=[logging._nameToLevel.keys()] + [k.lower() for k in logging._nameToLevel.keys()], help='Logging level', default='debug')
        self.args = parser.parse_args()

    def createUrwidUI(self, mainLoop):
        AttrMap = urwid.AttrMap
        Button = urwid.Button
        LineBox = urwid.LineBox
        Text = urwid.Text
        Filler = urwid.Filler
        Columns = urwid.Columns
        Divider = urwid.Divider
        Padding = urwid.Padding
        Pile = urwid.Pile
        Placeholder = urwid.WidgetPlaceholder

        def packLabeledText(labelTxt: str, defaultValue=''):
            label = Text(('bold', labelTxt), align='right')
            text = Text(defaultValue)
            attrName = labelTxt[:1].lower() + labelTxt[1:-1].replace(' ', '') + 'Text' if labelTxt else ''
            if attrName:
                setattr(self, attrName, text)
            minwidth = max([len(x) for x in self.leftFieldLabels + self.rightFieldLabels])
            return Columns([(minwidth, Filler(label, valign='top')), Filler(text, valign='top')], 1)
        
        def packLabeledButton(labelTxt: str, defaultValue='', callback: callable=None, userData=None):
            label = Text(('bold', labelTxt), align='right')
            button = AttrMap(Button(defaultValue, callback, userData), None, focus_map='reversed')
            attrName = labelTxt[:1].lower() + labelTxt[1:-1].replace(' ', '') + 'Button' if labelTxt else ''
            if attrName:
                setattr(self, attrName, button)
            return Columns([Filler(label, valign='top'), Filler(button, valign='top')], 1)

        widgets = [packLabeledText(labelTxt, 'not connected' if labelTxt == 'Host:' else '0'*11 if labelTxt == 'Head Block Num:' else '') for labelTxt in self.leftFieldLabels]
        # At least one child of a Pile must have weight 1 or the app will crash on mouse click in the Pile.
        leftColumn = Pile([(1, widget) for widget in widgets[:-1]] + [('weight', 1, widgets[-1])])

        widgets = [packLabeledButton(self.rightFieldLabels[0], callback=self.onVersionClick, userData=mainLoop)]
        widgets.extend([packLabeledText(labelTxt) for labelTxt in self.rightFieldLabels[1:]])
        widgets.insert(3, Filler(Divider()))
        rightColumn = Pile([(1, widget) for widget in widgets[:-1]] + [('weight', 1, widgets[-1])])

        def packLabeledList(labelTxt: str, attrName: str, focusChangedCallback: Callable):
            label = Text(('bold', labelTxt))
            listWalker = TextSimpleFocusListWalker([])
            #listWalker.set_focus_changed_callback(focusChangedCallback)
            #listWalker._focus_changed = MethodType(focusChangedCallback, listWalker)
            setattr(listWalker, 'name', attrName)
            setattr(self, attrName, listWalker)
            return Pile([('pack', label), ('weight', 1, urwid.ListBox(listWalker))]), listWalker

        def focus_changed(self, new_focus):
            logger.info(f'focus changed to {new_focus}')
            for listWalker in self.columns:
                logger.info(f'listwalker {id(listWalker)} self {id(self)}')
                if listWalker is not self:
                    listWalker.set_focus(new_focus)

        self.peerListPiles = []
        listWalkers = []
        for colName, attrName in self.peerColumns:
            p, l = packLabeledList(colName, attrName, focus_changed)
            self.peerListPiles.append(p)
            listWalkers.append(l)
        
        for listWalker in listWalkers:
            listWalker.columns = listWalkers

        columnedList = Columns([(0, self.peerListPiles[0]), # hidden connection ID column
                                ('weight', 1, self.peerListPiles[1]),
                                ('weight', 0.5, self.peerListPiles[2]),
                                ('weight', 2, self.peerListPiles[3])]+self.peerListPiles[4:],
                               dividechars=1, focus_column=0)
        self.peerLineBox = urwid.LineBox(columnedList, 'Peers:', 'left')

        self.mainView = Pile([Columns([leftColumn, rightColumn]), ('weight', 4, self.peerLineBox)])

        self.errorText = Text('')
        self.errorBox = LineBox(Pile([('weight', 4, Filler(self.errorText, 'top', 'flow')), Padding(Filler(Divider('\u2500'))), Filler(Padding(Button(('reversed', 'Close'), self.onDismissOverlay, mainLoop), 'center', len('< Close >')))]), 'Error:', 'left', 'error')
        self.errorOverlay = urwid.Overlay(self.errorBox, self.mainView, 'center', ('relative', 80), 
                                          'middle', ('relative', 80), min_width=24, min_height=8)

        widgets = [packLabeledText(labelTxt) for labelTxt in self.infoFieldLabels]
        widgets.append(Filler(Divider('\u2500')))
        widgets.append(Padding(Button(('reversed', 'Close'), self.onDismissOverlay, mainLoop), 'center', len('< Close >')))
        infoColumn = Filler(Pile([(1, widget) for widget in widgets[:-1]] + [('weight', 1, widgets[-1])]))
        self.infoBox = LineBox(infoColumn, 'Server Information:', 'left')
        self.infoOverlay = urwid.Overlay(self.infoBox, self.mainView, 'center', ('relative', 50), 
                                         'middle', ('relative', 25), min_width=28, min_height=8)

        return self.mainView


    def onVersionClick(self, button, mainLoop):
        mainLoop.widget = self.infoOverlay

    def onDismissOverlay(self, button, mainLoop):
        mainLoop.widget = self.mainView

    def update(self, mainLoop, userData=None):
        AttrMap = urwid.AttrMap
        Text = urwid.Text
        try:
            self.hostText.set_text(f'{self.args.host}:{self.args.port}')
            response = readMetrics(self.args.host, self.args.port)
        except (requests.ConnectionError, requests.ReadTimeout) as e:
            logger.error(str(e))
            self.errorText.set_text(str(e))
            mainLoop.widget = self.errorOverlay
        else:
            self.errorText.set_text('')
            if mainLoop.widget is self.errorOverlay:
                mainLoop.widget = self.mainView
            class bandwidthStats():
                def __init__(self, bytesReceived=0, bytesSent=0, connectionStarted=0):
                    self.bytesReceived = 0
                    self.bytesSent = 0
                    self.blockSyncBytesSent = 0
                    self.connectionStarted = 0
            for family in text_string_to_metric_families(response.text):
                bandwidths = {}
                for sample in family.samples:
                    listwalker = getattr(self, 'connectionIDLW')
                    if "connid" in sample.labels:
                        connID = sample.labels["connid"]
                        if connID not in listwalker:
                            startOffset = endOffset = len(listwalker)
                            listwalker.append(AttrMap(Text(connID), None, 'reversed'))
                        else:
                            startOffset = listwalker.index(connID)
                            endOffset = startOffset + 1
                    if sample.name in self.prometheusMetrics:
                        fieldName = self.fields.get(self.prometheusMetrics[sample.name])
                        field = getattr(self, fieldName)
                        field.set_text(str(int(sample.value)))
                    elif sample.name == 'nodeos_p2p_addr':
                        listwalker = getattr(self, 'ipAddressLW')
                        addr = ipaddress.ip_address(sample.labels["ipv6"])
                        host = f'{str(addr.ipv4_mapped) if addr.ipv4_mapped else str(addr)}'
                        listwalker[startOffset:endOffset] = [AttrMap(Text(host), None, 'reversed')]
                        listwalker = getattr(self, 'hostnameLW')
                        addr = sample.labels["address"]
                        listwalker[startOffset:endOffset] = [AttrMap(Text(addr), None, 'reversed')]
                    elif sample.name == 'nodeos_p2p_bytes_sent':
                        stats = bandwidths.get(connID, bandwidthStats())
                        stats.bytesSent = int(sample.value)
                        bandwidths[connID] = stats
                    elif fieldName == 'nodeos_p2p_block_sync_bytes_sent':
                        stats = bandwidths.get(connID, bandwidthStats())
                        stats.blockSyncBytesSent = int(sample.value)
                        bandwidths[connID] = stats
                    elif sample.name == 'nodeos_p2p_bytes_received':
                        stats = bandwidths.get(connID, bandwidthStats())
                        stats.bytesReceived = int(sample.value)
                        bandwidths[connID] = stats
                    elif sample.name == 'nodeos_p2p_connection_start_time':
                        stats = bandwidths.get(connID, bandwidthStats())
                        stats.connectionStarted = int(sample.value)
                        bandwidths[connID] = stats
                    elif sample.name == 'nodeos_p2p_connection_number':
                        pass
                    elif sample.name.startswith('nodeos_p2p_'):
                        fieldName = sample.name[len('nodeos_p2p_'):]
                        attrname = fieldName[:1] + fieldName.replace('_', ' ').title().replace(' ', '')[1:] + 'LW'
                        if hasattr(self, attrname):
                            listwalker = getattr(self, attrname)
                            listwalker[startOffset:endOffset] = [AttrMap(Text(self.peerMetricConversions[fieldName](sample.value)), None, 'reversed')]
                    elif sample.name == 'nodeos_p2p_connections':
                        if 'direction' in sample.labels:
                            fieldName = self.fields.get(self.prometheusMetrics[(sample.name, sample.labels['direction'])])
                            field = getattr(self, fieldName)
                            field.set_text(str(int(sample.value)))
                    elif sample.name == 'nodeos_info':
                        for infoLabel, infoValue in sample.labels.items():
                            fieldName = self.fields.get(self.prometheusMetrics[(sample.name, infoLabel)])
                            field = getattr(self, fieldName)
                            if type(field) is AttrMap:
                                field.original_widget.set_label(infoValue)
                            else:
                                field.set_text(infoValue)
                    else:
                        if sample.name not in self.ignoredPrometheusMetrics:
                            logger.warning(f'Received unhandled Prometheus metric {sample.name}')
                else:
                    if sample.name == 'nodeos_p2p_bytes_sent' or sample.name == 'nodeos_p2p_bytes_received' or sample.name == 'nodeos_p2p_block_sync_bytes_sent':
                        now = time.time_ns()
                        def updateBandwidth(connectedSeconds, listwalker, byteCount, startOffset, endOffset):
                            bps = byteCount/connectedSeconds
                            listwalker[startOffset:endOffset] = [AttrMap(Text(humanReadableBytesPerSecond(bps)), None, 'reversed')]
                        connIDListwalker = getattr(self, 'connectionIDLW')
                        for connID, stats in bandwidths.items():
                            startOffset = connIDListwalker.index(connID)
                            endOffset = startOffset + 1
                            connectedSeconds = (now - stats.connectionStarted)/1000000000
                            for listwalkerName, attrName in [('receiveBandwidthLW', 'bytesReceived'),
                                                              ('sendBandwidthLW', 'bytesSent'),
                                                              ('blockSyncBandwidthLW', 'blockSyncBytesSent')]:
                                listwalker = getattr(self, listwalkerName)
                                updateBandwidth(connectedSeconds, listwalker, getattr(stats, attrName), startOffset, endOffset)
        mainLoop.set_alarm_in(float(self.args.refresh_interval), self.update)

def exitOnQ(key):
    if key in ('q', 'Q'):
        raise urwid.ExitMainLoop()

if __name__ == '__main__':
    inst = netUtil()
    exePath = pathlib.Path(sys.argv[0])
    loggingLevel = getattr(logging, inst.args.log_level.upper(), None)
    if not isinstance(loggingLevel, int):
        raise ValueError(f'Invalid log level: {inst.args.log_level}')
    logging.basicConfig(filename=exePath.stem + '.log', filemode='w', level=loggingLevel)
    logger.info(f'Starting {sys.argv[0]}')
    palette = [('error', 'yellow,bold', 'default'),
               ('bold', 'default,bold', 'default'),
               ('dim', 'dark gray', 'default'),
               ('reversed', 'standout', ''),
              ]
    loop = urwid.MainLoop(urwid.Divider(), palette, screen=urwid.curses_display.Screen(), unhandled_input=exitOnQ, event_loop=None, pop_ups=True)
    ui = inst.createUrwidUI(loop)
    loop.widget = ui
    inst.update(loop)
    loop.run()
