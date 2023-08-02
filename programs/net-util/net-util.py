#!/usr/bin/env python3

import argparse
import logging
import pathlib
import requests
import sys
import time

import urwid
import urwid.curses_display

from prometheus_client.parser import text_string_to_metric_families
from urwid.canvas import apply_text_layout
from urwid.widget import WidgetError

logging.TRACE = 5
logging._levelToName[logging.TRACE] = 'TRACE'
logging._nameToLevel['TRACE'] = logging.TRACE
assert logging.TRACE < logging.DEBUG, 'Logging TRACE level expected to be lower than DEBUG'

logger = logging.getLogger(__name__)

def replacement_render(self, size, focus=False):
    """
    Render contents with wrapping and alignment.  Return canvas.

    See :meth:`Widget.render` for parameter details.

    >>> Text(u"important things").render((18,)).text # ... = b in Python 3
    [...'important things  ']
    >>> Text(u"important things").render((11,)).text
    [...'important  ', ...'things     ']
    """
    maxcol = size[0]
    text, attr = self.get_text()
    #assert isinstance(text, unicode)
    trans = self.get_line_translation( maxcol, (text,attr) )
    return apply_text_layout(text, attr, trans, maxcol)

def replacement_rows(self, size, focus=False):
    """
    Return the number of rows the rendered text requires.

    See :meth:`Widget.rows` for parameter details.

    >>> Text(u"important things").rows((18,))
    1
    >>> Text(u"important things").rows((11,))
    2
    """
    maxcol = size[0]
    return len(self.get_line_translation(maxcol))

def validate_size(widget, size, canv):
    """
    Raise a WidgetError if a canv does not match size size.
    """
    if (size and size[1:] != (0,) and size[0] != canv.cols()) or \
        (len(size)>1 and size[1] < canv.rows()):
        raise WidgetError("Widget %r rendered (%d x %d) canvas"
            " when passed size %r!" % (widget, canv.cols(),
            canv.rows(), size))

#urwid.Text.render = replacement_render
#urwid.Text.rows = replacement_rows
#urwid.widget.validate_size = validate_size

def humanReadableBytesPerSecond(bytes: int, telco:bool = False):
    power = 10**3 if telco else 2**10
    n = 0
    labels = {0: '', 1: 'K', 2: 'M', 3: 'G', 4: 'T'} if telco else {0: '', 1: 'Ki', 2: 'Mi', 3: 'Gi', 4: 'Ti'}
    while bytes > power:
        bytes /= power
        n += 1
    return f'{"~0" if bytes < 0.01 else format(bytes, ".2f")} {labels[n]}B/s'

class TextSimpleFocusListWalker(urwid.SimpleFocusListWalker):
    def __contains__(self, text):
        for element in self:
            if text == element.text:
                return True
        return False
    def index(self, text):
        '''Emulation of list index() method unfortunately much slower than the real thing but our lists are short'''
        for i, e in enumerate(self):
            if e.text == text:
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


def readMetrics():
    response = requests.get('http://localhost:8888/v1/prometheus/metrics', timeout=10)
    if response.status_code == 404:
        print(f'Prometheus metrics URL returned 404: {response.url}')
        raise urwid.ExitMainLoop()
    return response

class netUtil:
    def __init__(self):
        self.prometheusMetrics = {
            ('nodeos_info', 'server_version_string'): 'Nodeos Vers:',
            'nodeos_head_block_num': 'Head Block Num:',
            'nodeos_last_irreversible': 'LIB:',
            ('nodeos_p2p_connections','in'): 'Inbound P2P Connections:',
            ('nodeos_p2p_connections','out'): 'Outbound P2P Connections:',
            'nodeos_blocks_incoming_total': 'Total Incoming Blocks:',
            'nodeos_trxs_incoming_total': 'Total Incoming Trxs:',
            'nodeos_blocks_produced_total': 'Blocks Produced:',
            'nodeos_trxs_produced_total': 'Trxs Produced:',
            'nodeos_scheduled_trxs_total': 'Scheduled Trxs:',
            'nodeos_blacklisted_transactions_total': 'Blacklisted Trxs:',
            'nodeos_unapplied_transactions_total': 'Unapplied Trxs:',
            'nodeos_dropped_trxs_total': 'Dropped Trxs:',
            'nodeos_failed_p2p_connections_total': 'Failed P2P Connections:',
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
            'Nodeos Vers:',
            'LIB:',
            'Outbound P2P Connections:',
            'Total Incoming Trxs:',
            'Trxs Produced:',
            'Blacklisted Trxs:',
            'Dropped Trxs:',
        ]
        self.peerMetricConversions = {
            'hostname': lambda x: x[1:].replace('__', ':').replace('_', '.'),
            'accepting_blocks': lambda x: 'True' if x else 'False',
            'latency': lambda x: format(int(x)/1000000, '.2f') + ' ms',
            'bandwidth': lambda x: str(int(x)) + ' B/s',
            'last_received_block': lambda x: str(int(x)),
            'first_available_block': lambda x: str(int(x)),
            'last_available_block': lambda x: str(int(x)),
            'unique_first_block_count': lambda x: str(int(x)),
        }
        self.peerColumns = [
            ('\n\nHostname', 'hostnameLW'),
            ('\n\nLatency', 'latencyLW'),
            ('\nSend\nRate', 'sendBandwidthLW'),
            ('\nRcv\nRate', 'receiveBandwidthLW'),
            ('Last\nRcvd\nBlock', 'lastReceivedBlockLW'),
            ('Unique\nFirst\nBlks', 'uniqueFirstBlockCountLW'),
            ('First\nAvail\nBlk', 'firstAvailableBlockLW'),
            ('Last\nAvail\nBlk', 'lastAvailableBlockLW'),
            ('\nAcpt\nBlks', 'acceptingBlocksLW')
        ]
        self.fields = {k:v for k, v in zip(self.leftFieldLabels, [e[:1].lower() + e[1:-1].replace(' ', '') + 'Text' for e in self.leftFieldLabels])}
        self.fields.update({self.rightFieldLabels[0]: self.rightFieldLabels[0][:1].lower() + self.rightFieldLabels[0][1:-1].replace(' ', '') + 'Button'})
        self.fields.update({k:v for k, v in zip(self.rightFieldLabels[1:], [e[:1].lower() + e[1:-1].replace(' ', '') + 'Text' for e in self.rightFieldLabels[1:]])})
        
        parser = argparse.ArgumentParser(description='Terminal UI for monitoring and managing nodeos P2P connections')
        parser.add_argument('--log-level', choices=[logging._nameToLevel.keys()] + [k.lower() for k in logging._nameToLevel.keys()], help='Logging level', default='debug')
        self.args = parser.parse_args()

    def createUrwidUI(self):
        Button = urwid.Button
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
        
        def packLabeledButton(labelTxt: str, defaultValue='', callback: callable = None):
            label = Text(('bold', labelTxt), align='right')
            button = Button(defaultValue, callback)
            attrName = labelTxt[:1].lower() + labelTxt[1:-1].replace(' ', '') + 'Button' if labelTxt else ''
            if attrName:
                setattr(self, attrName, button)
            return Columns([Filler(label, valign='top'), Filler(button, valign='top')], 1)

        widgets = [packLabeledText(labelTxt, 'not connected' if labelTxt == 'Host:' else '0'*11 if labelTxt == 'Head Block Num:' else '') for labelTxt in self.leftFieldLabels]
        # At least one child of a Pile must have weight 1 or the app will crash on mouse click in the Pile.
        leftColumn = Pile([(1, widget) for widget in widgets[:-1]] + [('weight', 1, widgets[-1])])

        widgets = [packLabeledButton(self.rightFieldLabels[0], callback=self.onVersionClick)]
        widgets.extend([packLabeledText(labelTxt) for labelTxt in self.rightFieldLabels[1:]])
        widgets.insert(3, Filler(Divider()))
        rightColumn = Pile([(1, widget) for widget in widgets[:-1]] + [('weight', 1, widgets[-1])])

        self.errorText = Text(('error', ''))
        self.errorBox = urwid.LineBox(self.errorText, 'Error:', 'left', 'error')

        def packLabeledList(labelTxt: str, attrName: str):
            label = Text(('bold', labelTxt))
            listWalker = TextSimpleFocusListWalker([])
            setattr(listWalker, 'name', attrName)
            setattr(self, attrName, listWalker)
            return Pile([('pack', label), ('weight', 1, urwid.ListBox(listWalker))])

        self.peerListPiles = []
        for colName, attrName in self.peerColumns:
            self.peerListPiles.append(packLabeledList(colName, attrName))
        
        #for pile in self.peerListPiles:
        #    pile.setAllColumns(self.peerListPiles)

        def focus_changed(new_focus):
            logger.info('focus changed')
            for pile in self.peerListPiles[:-1]:
                pile.contents[-1][0].body.set_focus(new_focus)

        self.peerListPiles[0].contents[-1][0].body.set_focus_changed_callback(focus_changed)

        self.peerLineBox = urwid.LineBox(Columns([('weight', 2, self.peerListPiles[0])]+self.peerListPiles[1:],
                                                 dividechars=1, focus_column=0),
                                         'Peers:', 'left')

        #, Filler(Placeholder(self.errorText), valign='bottom')
        return Pile([Columns([leftColumn, rightColumn]), ('weight', 4, self.peerLineBox)])

    def onVersionClick(self, button):
        logger.info('Button clicked')

    def update(self, mainLoop, userData=None):
        try:
            response = readMetrics()
        except requests.ConnectionError as e:
            self.errorText.set_text(str(e))
            mainLoop.widget.contents[-1][0].original_widget = self.errorBox
        else:
            self.errorText.set_text('')
            self.hostText.set_text('localhost:8888')
            class bandwidthStats():
                def __init__(self, bytesReceived=0, bytesSent=0, connectionStarted=0):
                    self.bytesReceived = 0
                    self.bytesSent = 0
                    self.connectionStarted = 0
            mainLoop.widget.contents[-1][0].original_widget = self.errorText
            for family in text_string_to_metric_families(response.text):
                bandwidths = {}
                for sample in family.samples:
                    if sample.name in self.prometheusMetrics:
                        fieldName = self.fields.get(self.prometheusMetrics[sample.name])
                        field = getattr(self, fieldName)
                        field.set_text(str(int(sample.value)))
                    elif sample.name == 'nodeos_p2p_connections':
                        if 'direction' in sample.labels:
                            fieldName = self.fields.get(self.prometheusMetrics[(sample.name, sample.labels['direction'])])
                            field = getattr(self, fieldName)
                            field.set_text(str(int(sample.value)))
                        else:
                            hostLabel = next(iter(sample.labels))
                            fieldName = sample.labels[hostLabel]
                            host = hostLabel[1:].replace('__', ':').replace('_', '.')
                            listwalker = getattr(self, 'hostnameLW')
                            if host not in listwalker:
                                startOffset = endOffset = len(listwalker)
                                listwalker.append(urwid.Text(host))
                            else:
                                startOffset = listwalker.index(host)
                                endOffset = startOffset + 1
                            if fieldName == 'bytes_received':
                                bytesReceived = int(sample.value)
                                stats = bandwidths.get(host, bandwidthStats())
                                stats.bytesReceived = bytesReceived
                                bandwidths[host] = stats
                            elif fieldName == 'bytes_sent':
                                bytesSent = int(sample.value)
                                stats = bandwidths.get(host, bandwidthStats())
                                stats.bytesSent = bytesSent
                                bandwidths[host] = stats
                            elif fieldName == 'connection_start_time':
                                connectionStarted = int(sample.value)
                                stats = bandwidths.get(host, bandwidthStats())
                                stats.connectionStarted = connectionStarted
                                bandwidths[host] = stats
                                logger.info(f'length of bandwidth is {len(bandwidths)}')
                            else:
                                listwalker = getattr(self, fieldName[:1] + fieldName.replace('_', ' ').title().replace(' ', '')[1:] + 'LW')
                                listwalker[startOffset:endOffset] = [urwid.Text(self.peerMetricConversions[fieldName](sample.value))]
                    elif sample.name == 'nodeos_info':
                        fieldName = self.fields.get(self.prometheusMetrics[(sample.name, 'server_version_string')])
                        field = getattr(self, fieldName)
                        field.set_label(sample.labels['server_version_string'])
                    else:
                        if sample.name not in self.ignoredPrometheusMetrics:
                            logger.warning(f'Received unhandled Prometheus metric {sample.name}')
                else:
                    if sample.name == 'nodeos_p2p_connections':
                        now = time.time_ns()
                        hostListwalker = getattr(self, 'hostnameLW')
                        for host, stats in bandwidths.items():
                            startOffset = hostListwalker.index(host)
                            endOffset = startOffset + 1
                            connected_seconds = (now - stats.connectionStarted)/1000000000
                            listwalker = getattr(self, 'receiveBandwidthLW')
                            bps = stats.bytesReceived/connected_seconds
                            listwalker[startOffset:endOffset] = [urwid.Text(humanReadableBytesPerSecond(bps))]
                            listwalker = getattr(self, 'sendBandwidthLW')
                            bps = stats.bytesSent/connected_seconds
                            listwalker[startOffset:endOffset] = [urwid.Text(humanReadableBytesPerSecond(bps))]
        mainLoop.set_alarm_in(0.5, self.update)

def exitOnQ(key):
    if key in ('q', 'Q'):
        raise urwid.ExitMainLoop()

if __name__ == '__main__':
    inst = netUtil()
    exePath = pathlib.Path(sys.argv[0])
    loggingLevel = getattr(logging, inst.args.log_level.upper(), None)
    if not isinstance(loggingLevel, int):
        raise ValueError(f'Invalid log leve: {inst.args.log_level}')
    logging.basicConfig(filename=exePath.stem + '.log', filemode='w', level=loggingLevel)
    logger.info(f'Starting {sys.argv[0]}')
    palette = [('error', 'yellow,bold', 'default'),
               ('bold', 'default,bold', 'default'),
               ('dim', 'dark gray', 'default'),]
    loop = urwid.MainLoop(inst.createUrwidUI(), palette, screen=urwid.curses_display.Screen(), unhandled_input=exitOnQ, event_loop=None, pop_ups=True)
    loop.set_alarm_in(0.5, inst.update)
    loop.run()
