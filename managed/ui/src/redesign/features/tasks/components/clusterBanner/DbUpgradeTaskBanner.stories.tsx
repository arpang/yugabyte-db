import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';

import { dbUpgradeManagementPanelMswHandlers } from '@app/mocks/mock-data/dbUpgradeManagementPanelMswHandlers';
import {
  createDbUpgradePrecheckMetadataMock,
  createDbUpgradeTaskMock,
  DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
  defaultDbUpgradeSoftwareUpgradeProgress,
  tserverAzUpgradeStatesListWithSecondLastInProgress
} from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import {
  AZUpgradeStatus,
  CanaryPauseState,
  DbUpgradePrecheckStatus,
  TaskState,
  TaskType,
  type Task
} from '@app/redesign/features/tasks/dtos';
import type { UniverseSoftwareUpgradePrecheckResp } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { DbUpgradeTaskBanner } from './DbUpgradeTaskBanner';

const mockUniverse = generateUniverseMockResponse();

const defaultSoftwareUpgradeProgress = defaultDbUpgradeSoftwareUpgradeProgress();

/** Default docs canvas: paused after masters, matching {@link TaskState.PAUSED} on the base mock. */
const defaultDbUpgradeTask = createDbUpgradeTaskMock({
  softwareUpgradeProgress: {
    canaryPauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
    precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
    masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
      ...az,
      status: AZUpgradeStatus.COMPLETED
    })),
    tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map((az) => ({
      ...az,
      status: AZUpgradeStatus.NOT_STARTED
    }))
  }
});

const storyWithBannerMsw = (
  task: Task,
  precheckOverrides?: Partial<UniverseSoftwareUpgradePrecheckResp>
) => ({
  parameters: {
    msw: {
      handlers: {
        dbUpgradeTaskBanner: dbUpgradeManagementPanelMswHandlers(
          DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
          task,
          mockUniverse,
          createDbUpgradePrecheckMetadataMock(precheckOverrides ?? {})
        )
      }
    }
  }
});

const withCustomerId = (Story: ComponentType) => {
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('customerId', 'customer-uuid');
  }
  return <Story />;
};

const meta = {
  title: 'Tasks/DbUpgradeTaskBanner',
  component: DbUpgradeTaskBanner,
  tags: ['autodocs'],
  parameters: storyWithBannerMsw(defaultDbUpgradeTask).parameters,
  decorators: [withCustomerId, (Story: ComponentType) => <Story />],
  args: {
    universeUuid: DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
    task: defaultDbUpgradeTask
  }
} satisfies Meta<typeof DbUpgradeTaskBanner>;

export default meta;

type Story = StoryObj<typeof meta>;

const runningUpgradeSoftwareUpgradeProgress = {
  precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
  masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
    ...az,
    status: AZUpgradeStatus.COMPLETED
  })),
  tserverAZUpgradeStatesList: tserverAzUpgradeStatesListWithSecondLastInProgress(
    defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList
  )
};

const upgradeRunningTask = createDbUpgradeTaskMock({
  status: TaskState.RUNNING,
  softwareUpgradeProgress: runningUpgradeSoftwareUpgradeProgress
});

export const UpgradeRunning: Story = {
  ...storyWithBannerMsw(upgradeRunningTask),
  args: {
    task: upgradeRunningTask
  }
};

const upgradeRunningYsqlTask = createDbUpgradeTaskMock({
  status: TaskState.RUNNING,
  softwareUpgradeProgress: runningUpgradeSoftwareUpgradeProgress
});

export const UpgradeRunningYsqlMajorVersion: Story = {
  ...storyWithBannerMsw(upgradeRunningYsqlTask, {
    ysql_major_version_upgrade: true
  }),
  args: {
    task: upgradeRunningYsqlTask
  }
};

const pausedAfterMastersSoftwareUpgradeProgress = {
  canaryPauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
  precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
  masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
    ...az,
    status: AZUpgradeStatus.COMPLETED
  })),
  tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map((az) => ({
    ...az,
    status: AZUpgradeStatus.NOT_STARTED
  }))
};

const upgradePausedTask = createDbUpgradeTaskMock({
  status: TaskState.PAUSED,
  softwareUpgradeProgress: pausedAfterMastersSoftwareUpgradeProgress
});

export const UpgradePaused: Story = {
  ...storyWithBannerMsw(upgradePausedTask),
  args: {
    task: upgradePausedTask
  }
};

const allAzsCompletedSoftwareUpgradeProgress = {
  precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
  masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
    ...az,
    status: AZUpgradeStatus.COMPLETED
  })),
  tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map((az) => ({
    ...az,
    status: AZUpgradeStatus.COMPLETED
  }))
};

const upgradeSuccessfulPendingFinalizeTask = createDbUpgradeTaskMock({
  status: TaskState.SUCCESS,
  softwareUpgradeProgress: allAzsCompletedSoftwareUpgradeProgress
});

export const UpgradeSuccessfulPendingFinalize: Story = {
  ...storyWithBannerMsw(upgradeSuccessfulPendingFinalizeTask, {
    finalize_required: true,
    ysql_major_version_upgrade: true
  }),
  args: {
    task: upgradeSuccessfulPendingFinalizeTask
  }
};

const upgradeSuccessfulTask = createDbUpgradeTaskMock({
  status: TaskState.SUCCESS,
  softwareUpgradeProgress: allAzsCompletedSoftwareUpgradeProgress
});

export const UpgradeSuccessful: Story = {
  ...storyWithBannerMsw(upgradeSuccessfulTask),
  args: {
    task: upgradeSuccessfulTask
  }
};

const upgradeFailedTask = createDbUpgradeTaskMock({
  status: TaskState.FAILURE,
  softwareUpgradeProgress: {
    precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
    masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
      ...az,
      status: AZUpgradeStatus.COMPLETED
    })),
    tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map((az, index) => ({
      ...az,
      status: index === 0 ? AZUpgradeStatus.FAILED : AZUpgradeStatus.NOT_STARTED
    }))
  }
});

export const UpgradeFailed: Story = {
  ...storyWithBannerMsw(upgradeFailedTask),
  args: {
    task: upgradeFailedTask
  }
};

/** Renders nothing: banner only applies to software upgrade tasks. */
export const NonDbUpgradeTask: Story = {
  args: {
    task: createDbUpgradeTaskMock({ type: TaskType.EDIT as Task['type'] })
  }
};
