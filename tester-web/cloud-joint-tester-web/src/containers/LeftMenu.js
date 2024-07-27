import React, { PureComponent } from 'react';
import { connect } from 'react-redux';
import {
  updateActiveTestTag
} from '../store/actions/testAction';

import testListConfigMap from '../configs/testListConfigMap';

class LeftMenu extends PureComponent {

  handleTestTagClick = (id) => {
    this.props.updateActiveTestTag(id);
  }

  statusIndicator = (id) => {
    const { testResults = {} } = this.props;
    const status = testResults[id];
    if (status === true) {
      return <span className="statusIndicator outcomePass">{'PASS'}</span>;
    } else if (status === false) {
      return <span className="statusIndicator outcomeNotPass">{'FAIL'}</span>;
    } else {
      return <span className="statusIndicator">&nbsp;</span>;
    }
  }

  renderSubNav = (testContentSettings, subItems, handleSubNavClick, activeTestTag) => (
    subItems.map((subItemId) => (
      <li className={[
            'navSubitem',
            (activeTestTag === subItemId) ? 'active' : ''
          ].join(' ')} key={subItemId}>
        {this.statusIndicator(subItemId)}
        <button
          className="navBtn"
          id={subItemId}
          onClick={() => handleSubNavClick(subItemId)}
        >
            <span>{testContentSettings[subItemId].id}</span>{testContentSettings[subItemId].title}
        </button>
      </li>
    ))
  )

  renderNav = (leftMenu, testContentSettings, handleSubNavClick, activeTestTag) => (
    leftMenu.map((group) => (
      <ul className="navGroupWrap" key={`navGroup_${group.groupId}`}>
        {this.renderSubNav(testContentSettings, group.subItems, handleSubNavClick, activeTestTag)}
      </ul>
    ))
  )

  render () {
    const leftMenu = testListConfigMap[this.props.plantCategory].leftMenu;
    const testContentSettings = testListConfigMap[this.props.plantCategory].testContentSettings;

    return (
      <div className="leftMenuWrap">
        {this.renderNav(
          leftMenu,
          testContentSettings,
          this.handleTestTagClick,
          this.props.activeTestTag
          )}
      </div>
    );
  }
}

const mapStateToProps = ({ testReducer  }) => ({
  plantCategory: testReducer.plantCategory,
  activeTestTag: testReducer.activeTestTag,
  testResults: testReducer.testResults,
});
const mapDispatchToProps = {
  updateActiveTestTag,
};

export default connect(
  mapStateToProps,
  mapDispatchToProps
)(LeftMenu);
